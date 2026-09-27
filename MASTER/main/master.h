#pragma once
#include <stdbool.h>
#include <stddef.h>

/* HTTP handlers enqueue work and read snapshots; only the owner task touches
 * CAN or protocol state. Serial and web commands use the same parser. */
bool master_command(const char *line, char *result, size_t size);
char *master_json(void); /* Caller frees the allocated JSON string. */
void master_note(const char *format, ...) __attribute__((format(printf, 1, 2)));
void web_start(void);
void network_start(void);
bool network_credentials(const char *ssid, const char *password);
void network_address(char *buffer, size_t size);
