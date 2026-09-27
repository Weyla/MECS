#pragma once

#include "mio_io.h"

/* DI4-local state used to turn electrical GPIO edges into a CAN measurement. */
typedef struct {
  bool have_rise;
  bool have_fall;
  bool valid;
  uint32_t rise_us;
  uint32_t fall_us;
  uint32_t period_us;
  uint32_t high_us;
  uint32_t completed_us;
} di4_pwm_capture_t;

void di4_pwm_edge(di4_pwm_capture_t *capture, bool pin_high, uint32_t now_us);
mio_pwm_measurement_t di4_pwm_measure(di4_pwm_capture_t *capture,
                                      bool active_low, uint32_t now_us);
