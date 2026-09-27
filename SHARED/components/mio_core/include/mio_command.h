#pragma once
#include "mio_io.h"

/* Shared serial/web command grammar. Parsing only produces intent; it neither
 * sends CAN frames nor changes hardware. Names are case-sensitive. */
typedef enum {
    MIO_COMMAND_DISCOVER,
    MIO_COMMAND_NODES,
    MIO_COMMAND_HELP,
    MIO_COMMAND_REFRESH,
    MIO_COMMAND_PROPERTY
} mio_command_kind_t;
typedef struct {
    mio_command_kind_t kind;
    uint8_t node, channel, property;
    uint16_t value;
} mio_command_t;
bool mio_command_parse(const char *line, mio_command_t *command);
