/* DO4-only hardware: four GPIO outputs with independent LEDC PWM timers. */
#include "output_hardware.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static bool pwm_attached[MIO_CHANNELS];
static bool gpio_configured[MIO_CHANNELS];
static uint16_t pwm_frequency[MIO_CHANNELS];
static bool pwm_polarity[MIO_CHANNELS];

bool do4_apply_output(void *context, uint8_t channel,
                      const mio_channel_config_t *config,
                      bool output_is_active) {
  (void)context;

  const bool enabled = output_is_active && config->value;
  const bool should_use_pwm = enabled && config->mode == MIO_OUTPUT_PWM &&
                              config->duty_percent_x100 > 0 &&
                              config->duty_percent_x100 < 10000;

  if (should_use_pwm) {
    const uint32_t resolution =
        ledc_find_suitable_duty_resolution(40000000, config->frequency_hz);
    if (!resolution) {
      return false;
    }

    uint32_t duty =
        ((1u << resolution) * config->duty_percent_x100 + 5000) / 10000;
    /* LEDC reserves its maximum counter value. Keep non-endpoint values inside
     * it. */
    if (duty == 0) {
      duty = 1;
    }
    if (duty >= (1u << resolution)) {
      duty = (1u << resolution) - 1;
    }

    if (pwm_attached[channel] &&
        pwm_frequency[channel] == config->frequency_hz &&
        pwm_polarity[channel] == config->active_low) {
      /* Duty-only edits update the existing signal without detaching the pin.
       */
      return ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, channel, duty, 0) ==
             ESP_OK;
    }

    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = resolution,
        .timer_num = channel,
        .freq_hz = config->frequency_hz,
        .clk_cfg = LEDC_USE_XTAL_CLK,
    };
    if (ledc_timer_config(&timer) != ESP_OK) {
      return false;
    }

    const ledc_channel_config_t output = {
        .gpio_num = config->pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .timer_sel = channel,
        .duty = duty,
        .flags.output_invert = config->active_low,
    };
    if (ledc_channel_config(&output) != ESP_OK) {
      return false;
    }

    pwm_attached[channel] = true;
    gpio_configured[channel] = false;
    pwm_frequency[channel] = config->frequency_hz;
    pwm_polarity[channel] = config->active_low;
    return gpio_input_enable((gpio_num_t)config->pin) == ESP_OK;
  }

  /* Switch back from LEDC before applying a constant digital level. */
  if (pwm_attached[channel]) {
    if (ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0) != ESP_OK) {
      return false;
    }
    const ledc_channel_config_t release = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = channel,
        .deconfigure = true,
    };
    if (ledc_channel_config(&release) != ESP_OK) {
      return false;
    }
    pwm_attached[channel] = false;
  }

  const bool pin_active = enabled && (config->mode != MIO_OUTPUT_PWM ||
                                      config->duty_percent_x100 != 0);
  if (gpio_set_level((gpio_num_t)config->pin,
                     pin_active != config->active_low) != ESP_OK) {
    return false;
  }

  if (!gpio_configured[channel]) {
    const gpio_config_t output = {
        .pin_bit_mask = 1ULL << config->pin,
        .mode = GPIO_MODE_INPUT_OUTPUT,
    };
    gpio_configured[channel] = gpio_config(&output) == ESP_OK;
  }
  return gpio_configured[channel];
}

uint8_t do4_read_pins(const mio_io_node_t *outputs) {
  uint8_t levels = 0;
  for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
    if (gpio_get_level((gpio_num_t)outputs->config[channel].pin)) {
      levels |= (uint8_t)(1u << channel);
    }
  }
  return levels;
}
