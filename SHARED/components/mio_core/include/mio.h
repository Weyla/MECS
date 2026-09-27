#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIO_PROTOCOL_VERSION 3u
#define MIO_MAX_NODE_ID 127u
#define MIO_DISCOVER_ID 0x080u
#define MIO_ANNOUNCE_BASE 0x100u
#define MIO_BOARD_C3_DEMO 1u

typedef struct {
  uint32_t id;
  uint8_t length;
  bool extended;
  bool remote;
  uint8_t data[8];
} mio_frame_t;

typedef struct {
  uint8_t node_id;
  uint16_t board_type;
  uint8_t firmware_major;
  uint8_t firmware_minor;
  uint8_t firmware_patch;
} mio_identity_t;

typedef enum {
  MIO_ANNOUNCE_BOOT = 0,
  MIO_ANNOUNCE_REQUEST = 1,
  MIO_ANNOUNCE_RECOVERY = 2,
} mio_announce_reason_t;

/* send must copy the frame before returning. True means accepted, not
 * delivered. Callbacks execute synchronously in the caller's context. Do not
 * reenter this instance from callbacks. Serialize all access to an instance in
 * one task. */
typedef bool (*mio_send_fn)(void *context, const mio_frame_t *frame);
typedef void (*mio_discovered_fn)(void *context, const mio_identity_t *identity,
                                  mio_announce_reason_t reason);

typedef struct {
  mio_identity_t
      identity; /* node_id 0 is master; 1..127 are expansion nodes. */
  mio_send_fn send;
  mio_discovered_fn discovered; /* Optional, used only by the master. */
  void *context;
} mio_t;

typedef enum {
  MIO_RX_IGNORED,
  MIO_RX_INVALID,
  MIO_RX_HANDLED,
  MIO_RX_SEND_FAILED,
} mio_rx_result_t;

bool mio_init(mio_t *mio, mio_identity_t identity, mio_send_fn send,
              mio_discovered_fn discovered, void *context);
/* Return true only for a complete, supported master discovery request.
 * Node applications can use this helper in their receive loop, then call
 * mio_announce() explicitly so the response path is visible in board code. */
bool mio_is_discovery_request(const mio_frame_t *frame);
bool mio_request_discovery(mio_t *mio);
bool mio_announce(mio_t *mio, mio_announce_reason_t reason);
mio_rx_result_t mio_receive(mio_t *mio, const mio_frame_t *frame);

#ifdef __cplusplus
}
#endif
