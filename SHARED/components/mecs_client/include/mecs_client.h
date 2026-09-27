#pragma once

/*
 * Friendly master-side API for expansion I/O.
 *
 * This component owns discovery, heartbeat, cached node state and the
 * request/acknowledgement sequence. A framework adapter only needs to provide
 * CAN frame transmission and feed received frames to mecs_client_receive().
 * It contains no ESP-IDF, Arduino, FreeRTOS or ESPHome types.
 */
#include "mecs_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MECS_CLIENT_MAX_NODES 16u
#define MECS_CLIENT_QUEUE_LENGTH 16u
#define MECS_CLIENT_NODE_ONLINE_MS 1500u
#define MECS_CLIENT_REQUEST_TIMEOUT_MS 300u

typedef struct {
  mecs_send_fn send_frame; /* Must copy the frame before returning. */
  void *transport_context;
  uint16_t session; /* Nonzero; persist/increment it across master reboots. */
} mecs_client_config_t;

typedef struct {
  bool used;
  bool status_seen;
  uint32_t generation; /* Changes whenever previously applied state is lost. */
  mecs_identity_t identity;
  mecs_io_status_t status;
  uint32_t last_seen_ms;
  uint32_t last_status_ms;
  uint32_t value[MECS_CHANNELS + 1u][MECS_PROP_COUNT];
  uint32_t valid[MECS_CHANNELS + 1u];
  mecs_pwm_measurement_t measurement[MECS_CHANNELS];
  uint32_t measurement_ms[MECS_CHANNELS];
} mecs_client_node_t;

typedef struct {
  uint8_t node;
  uint8_t channel;
  uint8_t property;
  uint32_t value;
  bool read;
} mecs_client_queued_request_t;

typedef enum {
  MECS_CLIENT_EVENT_NODE_ANNOUNCED,
  MECS_CLIENT_EVENT_NODE_STATUS,
  MECS_CLIENT_EVENT_MEASUREMENT,
  MECS_CLIENT_EVENT_REQUEST_CONFIRMED,
  MECS_CLIENT_EVENT_REQUEST_REJECTED,
  MECS_CLIENT_EVENT_REQUEST_TIMEOUT,
} mecs_client_event_t;

typedef void (*mecs_client_event_fn)(void *context,
                                    mecs_client_event_t event,
                                    uint8_t node_id,
                                    uint8_t channel,
                                    uint8_t property,
                                    uint32_t value,
                                    mecs_error_t error);

typedef struct {
  mecs_client_config_t config;
  mecs_client_event_fn on_event;
  void *event_context;
  mecs_t discovery;
  mecs_client_node_t nodes[MECS_CLIENT_MAX_NODES];
  uint16_t next_transaction;
  uint32_t heartbeat_sent_ms;
  uint32_t discovery_sent_ms;
  uint32_t now_ms;
  bool heartbeat_sent;
  bool discovery_sent;
  bool discovery_requested;
  bool session_settling;
  uint32_t session_ready_ms;
  bool request_active;
  mecs_io_request_t request;
  uint8_t request_node;
  uint8_t request_channel;
  uint8_t request_property;
  uint32_t request_value;
  uint32_t request_started_ms;
  uint32_t request_sent_ms;
  uint8_t request_attempts;
  mecs_client_queued_request_t queue[MECS_CLIENT_QUEUE_LENGTH];
  uint8_t queue_head;
  uint8_t queue_count;
} mecs_client_t;

/* Initialize the client. session must be nonzero and should change after a
 * master reboot; this ensures nodes do not mistake a restarted master for the
 * owner of an earlier output-enable lease. */
bool mecs_client_begin(mecs_client_t *client,
                      const mecs_client_config_t *config,
                      mecs_client_event_fn on_event,
                      void *event_context);

/* Call regularly from the framework's main loop/task. Sends heartbeats,
 * discovery and pending requests, and expires unanswered requests. */
void mecs_client_loop(mecs_client_t *client, uint32_t now_ms);
/* Notify after transport recovery: discard stale work and establish the lease
 * again. A desired-state client then restores explicitly requested settings. */
void mecs_client_transport_reset(mecs_client_t *client);

/* Pass each received CAN frame here in application/task context. */
void mecs_client_receive(mecs_client_t *client,
                        const mecs_frame_t *frame,
                        uint32_t now_ms);

/* Ask all nodes to announce themselves. A successful return means the
 * discovery frame was accepted by the local CAN transport. */
bool mecs_client_discover(mecs_client_t *client);

/* Submit a typed property read or write. Requests enter a bounded FIFO and are
 * sent one at a time; the return value reports queue acceptance. Channel 255
 * selects a board-wide property. */
bool mecs_client_get(mecs_client_t *client,
                    uint8_t node_id,
                    uint8_t channel,
                    mecs_property_t property);
bool mecs_client_set(mecs_client_t *client,
                    uint8_t node_id,
                    uint8_t channel,
                    mecs_property_t property,
                    uint32_t value);

/* Read-only access to cached node information. Values are snapshots and may
 * become stale; use mecs_client_node_online() before acting on them. */
const mecs_client_node_t *mecs_client_node(const mecs_client_t *client,
                                         uint8_t node_id);
bool mecs_client_node_online(const mecs_client_node_t *node, uint32_t now_ms);
bool mecs_client_request_pending(const mecs_client_t *client);
/* Transaction IDs are never reused within a session. When this reports true,
 * persist a new nonzero session and apply it with mecs_client_set_session(). */
bool mecs_client_needs_new_session(const mecs_client_t *client);
bool mecs_client_set_session(mecs_client_t *client, uint16_t new_session);

#ifdef __cplusplus
}
#endif
