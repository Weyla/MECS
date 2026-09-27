#pragma once

#include "mio_io.h"

bool di4_hardware_start(const mio_io_node_t *inputs);
bool di4_apply_input(void *context, uint8_t channel,
                     const mio_channel_config_t *config,
                     bool unused_output_state);
uint8_t di4_read_pins(const mio_io_node_t *inputs);
mio_pwm_measurement_t di4_measure_pwm(uint8_t channel, bool active_low);
