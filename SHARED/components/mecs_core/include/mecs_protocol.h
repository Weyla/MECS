#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MECS_PROTOCOL_VERSION 3u
#define MECS_MAX_NODE_ID 127u
#define MECS_DISCOVER_ID 0x080u
#define MECS_ANNOUNCE_BASE 0x100u
#define MECS_BOARD_C3_DEMO 1u

typedef struct {
  uint32_t id;
  uint8_t length;
  bool extended;
  bool remote;
  uint8_t data[8];
} mecs_frame_t;

typedef struct {
  uint8_t node_id;
  uint16_t board_type;
  uint8_t firmware_major;
  uint8_t firmware_minor;
  uint8_t firmware_patch;
} mecs_identity_t;

typedef enum {
  MECS_ANNOUNCE_BOOT = 0,
  MECS_ANNOUNCE_REQUEST = 1,
  MECS_ANNOUNCE_RECOVERY = 2,
} mecs_announce_reason_t;

/* send must copy the frame before returning. True means accepted, not
 * delivered. Callbacks execute synchronously in the caller's context. Do not
 * reenter this instance from callbacks. Serialize all access to an instance in
 * one task. */
typedef bool (*mecs_send_fn)(void *context, const mecs_frame_t *frame);
typedef void (*mecs_discovered_fn)(void *context, const mecs_identity_t *identity,
                                  mecs_announce_reason_t reason);

typedef struct {
  mecs_identity_t
      identity; /* node_id 0 is master; 1..127 are expansion nodes. */
  mecs_send_fn send;
  mecs_discovered_fn discovered; /* Optional, used only by the master. */
  void *context;
} mecs_t;

typedef enum {
  MECS_RX_IGNORED,
  MECS_RX_INVALID,
  MECS_RX_HANDLED,
  MECS_RX_SEND_FAILED,
} mecs_rx_result_t;

bool mecs_init(mecs_t *mecs, mecs_identity_t identity, mecs_send_fn send,
              mecs_discovered_fn discovered, void *context);
/* Return true only for a complete, supported master discovery request.
 * Node applications can use this helper in their receive loop, then call
 * mecs_announce() explicitly so the response path is visible in board code. */
bool mecs_is_discovery_request(const mecs_frame_t *frame);
bool mecs_request_discovery(mecs_t *mecs);
bool mecs_announce(mecs_t *mecs, mecs_announce_reason_t reason);
mecs_rx_result_t mecs_receive(mecs_t *mecs, const mecs_frame_t *frame);

#ifdef __cplusplus
}
#endif
