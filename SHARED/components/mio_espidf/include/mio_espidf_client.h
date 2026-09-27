#pragma once

/* Optional helper for the portable master client. This is an application
 * convenience wrapper; customer code may instead poll mio_can_* directly. */
#include "mio_client.h"
#include "mio_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Process up to max_frames queued from CAN, then advance timers and transmit
 * pending client work. Call from the same task that owns the client. */
unsigned mio_espidf_client_poll(mio_client_t *client, uint32_t now_ms,
                               unsigned max_frames);

#ifdef __cplusplus
}
#endif
