/* Pure measurement math for DI4. The hardware adapter supplies edge levels and
 * microsecond timestamps; the master never runs this estimator. */
#include "pwm_measurement.h"

/* A complete rising/falling/rising sequence gives period and high time.
 * Invalid or incomplete cycles are reported as invalid instead of guessed. */
void di4_pwm_edge(di4_pwm_capture_t *capture, bool pin_high, uint32_t now_us) {
  if (pin_high) {
    capture->valid = false;
    if (capture->have_rise && capture->have_fall) {
      const uint32_t period = now_us - capture->rise_us;
      const uint32_t high_time = capture->fall_us - capture->rise_us;
      const bool period_ok =
          period >= MECS_PWM_MIN_PERIOD_US && period <= MECS_PWM_MAX_PERIOD_US;
      const bool pulse_widths_ok = high_time >= MECS_PWM_MIN_PULSE_US &&
                                   high_time < period &&
                                   period - high_time >= MECS_PWM_MIN_PULSE_US;
      if (period_ok && pulse_widths_ok) {
        capture->period_us = period;
        capture->high_us = high_time;
        capture->completed_us = now_us;
        capture->valid = true;
      }
    }
    capture->rise_us = now_us;
    capture->have_rise = true;
    capture->have_fall = false;
  } else if (capture->have_rise && !capture->have_fall) {
    capture->fall_us = now_us;
    capture->have_fall = true;
  } else {
    /* Repeated falling levels mean an edge was missed or arrived out of order.
     */
    capture->have_rise = false;
    capture->have_fall = false;
    capture->valid = false;
  }
}

/* Return the latest complete measurement, or invalid after the signal stops. */
mecs_pwm_measurement_t di4_pwm_measure(di4_pwm_capture_t *capture,
                                      bool active_low, uint32_t now_us) {
  uint32_t timeout_us = capture->period_us * 3;
  if (timeout_us < 100000) {
    timeout_us = 100000;
  }
  if (capture->valid &&
      (uint32_t)(now_us - capture->completed_us) > timeout_us) {
    capture->valid = false;
  }

  mecs_pwm_measurement_t result = {0};
  if (capture->valid) {
    const uint32_t active_time =
        active_low ? capture->period_us - capture->high_us : capture->high_us;
    result.valid = true;
    result.period_us = capture->period_us;
    result.duty_percent_x100 =
        (uint16_t)(((uint64_t)active_time * 10000 + capture->period_us / 2) /
                   capture->period_us);
  }
  return result;
}
