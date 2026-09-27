#pragma once

/* Low-level polling helper for applications that use the portable C client.
 * Most ESP-IDF applications can use MECSClient.h instead. */
#include "mecs_client.h"
#include "mecs_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Process up to max_frames queued from CAN, then advance timers and transmit
 * pending client work. Call from the same task that owns the client. */
unsigned mecs_espidf_client_poll(mecs_client_t *client, uint32_t now_ms,
                               unsigned max_frames);

#ifdef __cplusplus
}
#endif
