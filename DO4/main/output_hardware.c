/* DO4-only hardware: four GPIO outputs with independent LEDC PWM timers. */
#include "output_hardware.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static bool pwm_attached[MECS_CHANNELS];
static bool gpio_configured[MECS_CHANNELS];
static gpio_num_t pwm_gpio[MECS_CHANNELS];
static uint16_t pwm_frequency[MECS_CHANNELS];
static bool pwm_polarity[MECS_CHANNELS];

/* Stop PWM and return its GPIO to normal GPIO control before reusing the pin.
 * LEDC's fade service is intentionally not used: output changes are immediate
 * and do not need a ramp. */
static bool detach_pwm(uint8_t channel) {
  if (!pwm_attached[channel]) {
    return true;
  }

  /* Keep the pin at its inactive physical level while LEDC releases it. */
  const uint32_t inactive_level = pwm_polarity[channel] ? 1u : 0u;
  if (ledc_stop(LEDC_LOW_SPEED_MODE, channel, inactive_level) != ESP_OK) {
    return false;
  }

  /* `ledc_channel_config_t.deconfigure` is not available in ESP-IDF 5.5.
   * Resetting the routed pin releases the GPIO matrix; the stopped LEDC
   * channel can be configured again when this output returns to PWM mode. */
  if (gpio_reset_pin(pwm_gpio[channel]) != ESP_OK) {
    return false;
  }

  pwm_attached[channel] = false;
  pwm_gpio[channel] = GPIO_NUM_NC;
  gpio_configured[channel] = false;
  return true;
}

bool do4_apply_output(void *context, uint8_t channel,
                      const mecs_channel_config_t *config,
                      bool output_is_active) {
  (void)context;

  const bool enabled = output_is_active && config->value;
  const bool should_use_pwm = enabled && config->mode == MECS_OUTPUT_PWM &&
                              config->duty_percent_x1000 > 0 &&
                              config->duty_percent_x1000 < 100000;

  if (should_use_pwm) {
    const uint32_t resolution =
        ledc_find_suitable_duty_resolution(40000000, config->frequency_hz);
    if (!resolution) {
      return false;
    }

    uint32_t duty =
        ((1ULL << resolution) * config->duty_percent_x1000 + 50000) / 100000;
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
      /* Duty-only changes do not use LEDC fading. Calls are serialized by the
       * DO4 owner task, so the ordinary set/update pair is safe here. */
      if (ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty) != ESP_OK) {
        return false;
      }
      return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel) == ESP_OK;
    }

    /* A frequency or polarity change needs a fresh timer/channel setup. First
     * release any existing LEDC claim so the GPIO matrix is not double-owned. */
    if (!detach_pwm(channel)) {
      return false;
    }

    /* Digital mode reserves the GPIO output path. Reset it before LEDC claims
     * that same path, otherwise ESP-IDF reports a GPIO conflict on attachment. */
    if (gpio_configured[channel]) {
      if (gpio_reset_pin((gpio_num_t)config->pin) != ESP_OK) {
        return false;
      }
      gpio_configured[channel] = false;
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
    pwm_gpio[channel] = (gpio_num_t)config->pin;
    gpio_configured[channel] = false;
    pwm_frequency[channel] = config->frequency_hz;
    pwm_polarity[channel] = config->active_low;
    return gpio_input_enable((gpio_num_t)config->pin) == ESP_OK;
  }

  /* Switch back from LEDC before applying a constant digital level. */
  if (!detach_pwm(channel)) {
    return false;
  }

  const bool pin_active = enabled && (config->mode != MECS_OUTPUT_PWM ||
                                      config->duty_percent_x1000 != 0);
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

uint8_t do4_read_pins(const mecs_io_node_t *outputs) {
  uint8_t levels = 0;
  for (unsigned channel = 0; channel < MECS_CHANNELS; ++channel) {
    if (gpio_get_level((gpio_num_t)outputs->config[channel].pin)) {
      levels |= (uint8_t)(1u << channel);
    }
  }
  return levels;
}
