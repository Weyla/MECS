#pragma once
#include "mecs_io.h"

/* Shared serial/web command grammar. Parsing only produces intent; it neither
 * sends CAN frames nor changes hardware. Names are case-sensitive. */
typedef enum {
    MECS_COMMAND_DISCOVER,
    MECS_COMMAND_NODES,
    MECS_COMMAND_HELP,
    MECS_COMMAND_REFRESH,
    MECS_COMMAND_PROPERTY
} mecs_command_kind_t;
typedef struct {
    mecs_command_kind_t kind;
    uint8_t node, channel, property;
    uint32_t value;
} mecs_command_t;
bool mecs_command_parse(const char *line, mecs_command_t *command);
