#pragma once

#include "mecs_io.h"

bool do4_apply_output(void *context, uint8_t channel,
                      const mecs_channel_config_t *config,
                      bool output_is_active);
uint8_t do4_read_pins(const mecs_io_node_t *outputs);
