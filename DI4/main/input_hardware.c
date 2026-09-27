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
  uint32_t edge_window_us;
  unsigned edges_in_window;
} input_capture_t;

static input_capture_t captures[MIO_CHANNELS];
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
   * The task retries capture after a brief cooling interval. */
  if (++capture->edges_in_window > 240) {
    capture->paused_for_rate = true;
    capture->measurement.valid = false;
    gpio_intr_disable(capture->pin);
  } else {
    di4_pwm_edge(&capture->measurement, pin_high, now_us);
  }
  portEXIT_CRITICAL_ISR(&capture_lock);
}

bool di4_hardware_start(const mio_io_node_t *inputs) {
  if (inputs->board_type != MIO_BOARD_DI4) {
    return false;
  }
  if (gpio_install_isr_service(0) != ESP_OK) {
    return false;
  }
  for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
    captures[channel].pin = (gpio_num_t)inputs->config[channel].pin;
    if (gpio_isr_handler_add(captures[channel].pin, on_input_edge,
                             (void *)(uintptr_t)channel) != ESP_OK) {
      return false;
    }
  }
  return true;
}

bool di4_apply_input(void *context, uint8_t channel,
                     const mio_channel_config_t *config,
                     bool unused_output_state) {
  (void)context;
  (void)unused_output_state;

  /* Reconfigure the input and discard a partial/old PWM cycle. */
  gpio_intr_disable((gpio_num_t)config->pin);
  portENTER_CRITICAL(&capture_lock);
  memset(&captures[channel].measurement, 0,
         sizeof(captures[channel].measurement));
  captures[channel].paused_for_rate = false;
  captures[channel].edges_in_window = 0;
  captures[channel].edge_window_us = (uint32_t)esp_timer_get_time();
  portEXIT_CRITICAL(&capture_lock);

  const gpio_config_t input = {
      .pin_bit_mask = 1ULL << config->pin,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = config->pull == MIO_PULL_UP,
      .pull_down_en = config->pull == MIO_PULL_DOWN,
      .intr_type = config->filter == MIO_FILTER_PWM ? GPIO_INTR_ANYEDGE
                                                    : GPIO_INTR_DISABLE,
  };
  return gpio_config(&input) == ESP_OK;
}

uint8_t di4_read_pins(const mio_io_node_t *inputs) {
  uint8_t levels = 0;
  for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
    if (gpio_get_level((gpio_num_t)inputs->config[channel].pin)) {
      levels |= (uint8_t)(1u << channel);
    }
  }
  return levels;
}

mio_pwm_measurement_t di4_measure_pwm(uint8_t channel, bool active_low) {
  portENTER_CRITICAL(&capture_lock);
  /* Read the clock while holding the same lock as the ISR snapshot. */
  const uint32_t now_us = (uint32_t)esp_timer_get_time();
  input_capture_t *capture = &captures[channel];
  if (capture->paused_for_rate &&
      (uint32_t)(now_us - capture->edge_window_us) >= 100000) {
    memset(&capture->measurement, 0, sizeof(capture->measurement));
    capture->paused_for_rate = false;
    capture->edges_in_window = 0;
    capture->edge_window_us = now_us;
    gpio_intr_enable(capture->pin);
  }
  const mio_pwm_measurement_t result =
      di4_pwm_measure(&capture->measurement, active_low, now_us);
  portEXIT_CRITICAL(&capture_lock);
  return result;
}
