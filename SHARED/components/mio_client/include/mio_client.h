#pragma once

/*
 * Friendly master-side API for expansion I/O.
 *
 * This component owns discovery, heartbeat, cached node state and the
 * request/acknowledgement sequence. A framework adapter only needs to provide
 * CAN frame transmission and feed received frames to mio_client_receive().
 * It contains no ESP-IDF, Arduino, FreeRTOS or ESPHome types.
 */
#include "mio_io.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIO_CLIENT_MAX_NODES 16u
#define MIO_CLIENT_QUEUE_LENGTH 16u
#define MIO_CLIENT_NODE_ONLINE_MS 1500u
#define MIO_CLIENT_REQUEST_TIMEOUT_MS 300u

typedef struct {
  mio_send_fn send_frame; /* Must copy the frame before returning. */
  void *transport_context;
  uint16_t session; /* Nonzero; persist/increment it across master reboots. */
} mio_client_config_t;

typedef struct {
  bool used;
  bool status_seen;
  mio_identity_t identity;
  mio_io_status_t status;
  uint32_t last_seen_ms;
  uint32_t last_status_ms;
  uint16_t value[MIO_CHANNELS + 1u][MIO_PROP_COUNT];
  uint32_t valid[MIO_CHANNELS + 1u];
  mio_pwm_measurement_t measurement[MIO_CHANNELS];
  uint32_t measurement_ms[MIO_CHANNELS];
} mio_client_node_t;

typedef struct {
  uint8_t node;
  uint8_t channel;
  uint8_t property;
  uint16_t value;
  bool read;
} mio_client_queued_request_t;

typedef enum {
  MIO_CLIENT_EVENT_NODE_ANNOUNCED,
  MIO_CLIENT_EVENT_NODE_STATUS,
  MIO_CLIENT_EVENT_MEASUREMENT,
  MIO_CLIENT_EVENT_REQUEST_CONFIRMED,
  MIO_CLIENT_EVENT_REQUEST_REJECTED,
  MIO_CLIENT_EVENT_REQUEST_TIMEOUT,
} mio_client_event_t;

typedef void (*mio_client_event_fn)(void *context,
                                    mio_client_event_t event,
                                    uint8_t node_id,
                                    uint8_t channel,
                                    uint8_t property,
                                    uint16_t value,
                                    mio_error_t error);

typedef struct {
  mio_client_config_t config;
  mio_client_event_fn on_event;
  void *event_context;
  mio_t discovery;
  mio_client_node_t nodes[MIO_CLIENT_MAX_NODES];
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
  mio_io_request_t request;
  uint8_t request_node;
  uint8_t request_channel;
  uint8_t request_property;
  uint16_t request_value;
  uint32_t request_started_ms;
  uint32_t request_sent_ms;
  uint8_t request_attempts;
  mio_client_queued_request_t queue[MIO_CLIENT_QUEUE_LENGTH];
  uint8_t queue_head;
  uint8_t queue_count;
} mio_client_t;

/* Initialize the client. session must be nonzero and should change after a
 * master reboot; this ensures nodes do not mistake a restarted master for the
 * owner of an earlier output-enable lease. */
bool mio_client_begin(mio_client_t *client,
                      const mio_client_config_t *config,
                      mio_client_event_fn on_event,
                      void *event_context);

/* Call regularly from the framework's main loop/task. Sends heartbeats,
 * discovery and pending requests, and expires unanswered requests. */
void mio_client_loop(mio_client_t *client, uint32_t now_ms);

/* Pass each received CAN frame here in application/task context. */
void mio_client_receive(mio_client_t *client,
                        const mio_frame_t *frame,
                        uint32_t now_ms);

/* Ask all nodes to announce themselves. A successful return means the
 * discovery frame was accepted by the local CAN transport. */
bool mio_client_discover(mio_client_t *client);

/* Submit a typed property read or write. Requests enter a bounded FIFO and are
 * sent one at a time; the return value reports queue acceptance. Channel 255
 * selects a board-wide property. */
bool mio_client_get(mio_client_t *client,
                    uint8_t node_id,
                    uint8_t channel,
                    mio_property_t property);
bool mio_client_set(mio_client_t *client,
                    uint8_t node_id,
                    uint8_t channel,
                    mio_property_t property,
                    uint16_t value);

/* Read-only access to cached node information. Values are snapshots and may
 * become stale; use mio_client_node_online() before acting on them. */
const mio_client_node_t *mio_client_node(const mio_client_t *client,
                                         uint8_t node_id);
bool mio_client_node_online(const mio_client_node_t *node, uint32_t now_ms);
bool mio_client_request_pending(const mio_client_t *client);
/* Transaction IDs are never reused within a session. When this reports true,
 * persist a new nonzero session and apply it with mio_client_set_session(). */
bool mio_client_needs_new_session(const mio_client_t *client);
bool mio_client_set_session(mio_client_t *client, uint16_t new_session);

#ifdef __cplusplus
}
#endif
