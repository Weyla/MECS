/* DI4-only hardware: GPIO inputs and interrupt-based PWM edge capture. */
#include "input_hardware.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "pwm_measurement.h"
#include <string.h>

typedef struct {
  di4_pwm_capture_t measurement;
  gpio_num_t pin;
  bool paused_for_rate;
  bool interrupt_disabled;
  uint32_t edge_window_us;
  unsigned edges_in_window;
} input_capture_t;

static input_capture_t captures[MECS_CHANNELS];
static portMUX_TYPE capture_lock = portMUX_INITIALIZER_UNLOCKED;

/* Keep the interrupt short: timestamp one edge and update local capture state.
 * It never logs, allocates memory or sends CAN. */
static void on_input_edge(void *argument) {
  const unsigned channel = (unsigned)(uintptr_t)argument;
  const uint32_t now_us = (uint32_t)esp_timer_get_time();
  const bool pin_high = gpio_get_level(captures[channel].pin) != 0;

  portENTER_CRITICAL_ISR(&capture_lock);
  input_capture_t *capture = &captures[channel];
  if ((uint32_t)(now_us - capture->edge_window_us) >= 10000) {
    capture->edge_window_us = now_us;
    capture->edges_in_window = 0;
  }

  /* Pause if a signal exceeds the prototype's measured interrupt budget.
   * Mark it here; GPIO driver calls run later in task context. */
  if (capture->paused_for_rate) {
    /* The owner task will disable this interrupt as soon as it observes the
     * pause. Ignore any additional edges in the meantime. */
  } else if (++capture->edges_in_window > 240) {
    capture->paused_for_rate = true;
    capture->edge_window_us = now_us;
    capture->measurement.valid = false;
  } else {
    di4_pwm_edge(&capture->measurement, pin_high, now_us);
  }
  portEXIT_CRITICAL_ISR(&capture_lock);
}

bool di4_hardware_start(const mecs_io_node_t *inputs) {
  if (inputs->board_type != MECS_BOARD_DI4) {
    return false;
  }
  if (gpio_install_isr_service(0) != ESP_OK) {
    return false;
  }
  for (unsigned channel = 0; channel < MECS_CHANNELS; ++channel) {
    captures[channel].pin = (gpio_num_t)inputs->config[channel].pin;
    if (gpio_isr_handler_add(captures[channel].pin, on_input_edge,
                             (void *)(uintptr_t)channel) != ESP_OK) {
      return false;
    }
  }
  return true;
}

bool di4_apply_input(void *context, uint8_t channel,
                     const mecs_channel_config_t *config,
                     bool unused_output_state) {
  (void)context;
  (void)unused_output_state;

  /* Reconfigure the input and discard a partial/old PWM cycle. */
  gpio_intr_disable((gpio_num_t)config->pin);
  portENTER_CRITICAL(&capture_lock);
  memset(&captures[channel].measurement, 0,
         sizeof(captures[channel].measurement));
  captures[channel].paused_for_rate = false;
  captures[channel].interrupt_disabled = false;
  captures[channel].edges_in_window = 0;
  captures[channel].edge_window_us = (uint32_t)esp_timer_get_time();
  portEXIT_CRITICAL(&capture_lock);

  const gpio_config_t input = {
      .pin_bit_mask = 1ULL << config->pin,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = config->pull == MECS_PULL_UP,
      .pull_down_en = config->pull == MECS_PULL_DOWN,
      .intr_type = config->filter == MECS_FILTER_PWM ? GPIO_INTR_ANYEDGE
                                                    : GPIO_INTR_DISABLE,
  };
  return gpio_config(&input) == ESP_OK;
}

uint8_t di4_read_pins(const mecs_io_node_t *inputs) {
  uint8_t levels = 0;
  for (unsigned channel = 0; channel < MECS_CHANNELS; ++channel) {
    if (gpio_get_level((gpio_num_t)inputs->config[channel].pin)) {
      levels |= (uint8_t)(1u << channel);
    }
  }
  return levels;
}

mecs_pwm_measurement_t di4_measure_pwm(uint8_t channel, bool active_low) {
  bool disable_interrupt = false;
  bool enable_interrupt = false;
  gpio_num_t pin;
  portENTER_CRITICAL(&capture_lock);
  /* Read the clock while holding the same lock as the ISR snapshot. */
  const uint32_t now_us = (uint32_t)esp_timer_get_time();
  input_capture_t *capture = &captures[channel];
  pin = capture->pin;
  if (capture->paused_for_rate && !capture->interrupt_disabled) {
    capture->interrupt_disabled = true;
    disable_interrupt = true;
  }
  if (capture->paused_for_rate && capture->interrupt_disabled &&
      (uint32_t)(now_us - capture->edge_window_us) >= 100000) {
    memset(&capture->measurement, 0, sizeof(capture->measurement));
    capture->paused_for_rate = false;
    capture->interrupt_disabled = false;
    capture->edges_in_window = 0;
    capture->edge_window_us = now_us;
    enable_interrupt = true;
  }
  const mecs_pwm_measurement_t result =
      di4_pwm_measure(&capture->measurement, active_low, now_us);
  portEXIT_CRITICAL(&capture_lock);
  /* The GPIO driver may take locks, so never call it from an ISR or while the
   * capture spinlock is held. The paused flag makes the handoff safe. */
  if (disable_interrupt && gpio_intr_disable(pin) != ESP_OK) {
    /* Leave the capture paused and retry the driver operation on the next
     * owner-loop pass instead of assuming the interrupt was disabled. */
    portENTER_CRITICAL(&capture_lock);
    captures[channel].interrupt_disabled = false;
    portEXIT_CRITICAL(&capture_lock);
  }
  if (enable_interrupt && gpio_intr_enable(pin) != ESP_OK) {
    /* The signal remains invalid. Retry after another quiet interval. */
    portENTER_CRITICAL(&capture_lock);
    captures[channel].paused_for_rate = true;
    captures[channel].interrupt_disabled = true;
    captures[channel].edge_window_us = now_us;
    portEXIT_CRITICAL(&capture_lock);
  }
  return result;
}
