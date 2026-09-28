/* Framework-independent master client. Keep transport work in the caller's
 * loop so the same implementation can run from ESP-IDF, Arduino or ESPHome. */
#include "mecs_client.h"

#include <string.h>

#define MECS_CLIENT_HEARTBEAT_MS 250u
#define MECS_CLIENT_DISCOVERY_MS 5000u
#define MECS_CLIENT_MAX_ATTEMPTS 3u
#define MECS_CLIENT_REQUEST_LIFETIME_MS 1500u

static unsigned slot_for(uint8_t channel) {
  return channel == MECS_GLOBAL_CHANNEL ? MECS_CHANNELS : channel;
}

static uint8_t role_for_board(uint16_t board_type) {
  if (board_type == MECS_BOARD_DI4) {
    return 1u;
  }
  if (board_type == MECS_BOARD_DO4) {
    return 2u;
  }
  return 0u;
}

static mecs_client_node_t *find_node(mecs_client_t *client, uint8_t node_id) {
  for (unsigned i = 0; i < MECS_CLIENT_MAX_NODES; ++i) {
    if (client->nodes[i].used &&
        client->nodes[i].identity.node_id == node_id) {
      return &client->nodes[i];
    }
  }
  return NULL;
}

static void emit(mecs_client_t *client, mecs_client_event_t event,
                 uint8_t node, uint8_t channel, uint8_t property,
                 uint32_t value, mecs_error_t error) {
  if (client->on_event) {
    client->on_event(client->event_context, event, node, channel,
                     property, value, error);
  }
}

/* A command from an earlier node lifetime must never be delivered after its
 * replacement has booted. Remove only this node's work, preserving the order
 * of other nodes' requests. Late replies then fail the transaction check. */
static void cancel_node_requests(mecs_client_t *client, uint8_t node_id) {
  if (client->request_active && client->request_node == node_id) {
    client->request_active = false;
    emit(client, MECS_CLIENT_EVENT_REQUEST_REJECTED, node_id,
         client->request_channel, client->request_property,
         client->request_value, MECS_ERR_OFFLINE);
  }
  const unsigned count = client->queue_count;
  for (unsigned i = 0; i < count; ++i) {
    const mecs_client_queued_request_t next = client->queue[client->queue_head];
    client->queue_head = (client->queue_head + 1u) % MECS_CLIENT_QUEUE_LENGTH;
    --client->queue_count;
    if (next.node == node_id) {
      emit(client, MECS_CLIENT_EVENT_REQUEST_REJECTED, node_id,
           next.channel, next.property, next.value, MECS_ERR_OFFLINE);
    } else {
      const unsigned tail = (client->queue_head + client->queue_count) %
                            MECS_CLIENT_QUEUE_LENGTH;
      client->queue[tail] = next;
      ++client->queue_count;
    }
  }
}

static void invalidate_node(mecs_client_t *client, mecs_client_node_t *node) {
  ++node->generation;
  memset(node->valid, 0, sizeof(node->valid));
  memset(node->measurement, 0, sizeof(node->measurement));
  cancel_node_requests(client, node->identity.node_id);
  /* Boot/status traffic can arrive between the periodic heartbeats. Send a
   * heartbeat immediately and wait for a live status before configuration. */
  client->heartbeat_sent = false;
}

/* mecs_t shares one callback context between transport and discovery. This
 * small trampoline keeps transport ownership in the public client config. */
static bool client_send(void *context, const mecs_frame_t *frame) {
  mecs_client_t *client = context;
  return client->config.send_frame(client->config.transport_context, frame);
}

static void discovered(void *context, const mecs_identity_t *identity,
                       mecs_announce_reason_t reason) {
  mecs_client_t *client = context;
  mecs_client_node_t *node = find_node(client, identity->node_id);
  if (!node) {
    for (unsigned i = 0; i < MECS_CLIENT_MAX_NODES; ++i) {
      if (!client->nodes[i].used) {
        node = &client->nodes[i];
        break;
      }
    }
  }
  if (!node) {
    return; /* Bounded registry: ignore excess nodes safely. */
  }

  const bool rebooted = node->used &&
      (reason == MECS_ANNOUNCE_BOOT ||
       reason == MECS_ANNOUNCE_RECOVERY ||
       node->identity.board_type != identity->board_type);
  if (!node->used || rebooted) {
    const uint32_t generation = node->generation + 1u;
    if (rebooted) {
      cancel_node_requests(client, identity->node_id);
    }
    memset(node, 0, sizeof(*node));
    node->generation = generation;
    client->heartbeat_sent = false;
  }
  node->used = true;
  node->identity = *identity;
  emit(client, MECS_CLIENT_EVENT_NODE_ANNOUNCED, identity->node_id,
       MECS_GLOBAL_CHANNEL, 0u, identity->board_type, MECS_OK);
}

bool mecs_client_begin(mecs_client_t *client,
                      const mecs_client_config_t *config,
                      mecs_client_event_fn on_event,
                      void *event_context) {
  if (!client || !config || !config->send_frame || !config->session) {
    return false;
  }
  memset(client, 0, sizeof(*client));
  client->config = *config;
  client->on_event = on_event;
  client->event_context = event_context;
  const mecs_identity_t master_identity = {0};
  return mecs_init(&client->discovery, master_identity, client_send, discovered,
                  client);
}

bool mecs_client_discover(mecs_client_t *client) {
  if (!client || !mecs_request_discovery(&client->discovery)) {
    return false;
  }
  client->discovery_requested = false;
  client->discovery_sent = true;
  client->discovery_sent_ms = client->now_ms;
  return true;
}

void mecs_client_transport_reset(mecs_client_t *client) {
  if (!client) {
    return;
  }
  for (unsigned i = 0; i < MECS_CLIENT_MAX_NODES; ++i) {
    if (client->nodes[i].used) {
      invalidate_node(client, &client->nodes[i]);
      client->nodes[i].status_seen = false;
    }
  }
  client->heartbeat_sent = false;
  client->discovery_requested = true;
}

/* Receive is commonly serviced before loop(). Enforce the same deadline in
 * both paths so a delayed ACK cannot revive an expired request. */
static bool expire_request(mecs_client_t *client, uint32_t now_ms) {
  if (!client->request_active ||
      ((uint32_t)(now_ms - client->request_started_ms) <
           MECS_CLIENT_REQUEST_LIFETIME_MS &&
       (client->request_attempts < MECS_CLIENT_MAX_ATTEMPTS ||
        (uint32_t)(now_ms - client->request_sent_ms) <
            MECS_CLIENT_REQUEST_TIMEOUT_MS))) {
    return false;
  }
  client->request_active = false;
  mecs_client_node_t *node = find_node(client, client->request_node);
  if (node) {
    node->valid[slot_for(client->request_channel)] &=
        ~(1u << client->request_property);
  }
  emit(client, MECS_CLIENT_EVENT_REQUEST_TIMEOUT, client->request_node,
       client->request_channel, client->request_property,
       client->request_value, MECS_ERR_OFFLINE);
  return true;
}

void mecs_client_loop(mecs_client_t *client, uint32_t now_ms) {
  if (!client || !client->config.send_frame) {
    return;
  }
  client->now_ms = now_ms;

  mecs_frame_t frame;
  if (!client->heartbeat_sent ||
      (uint32_t)(now_ms - client->heartbeat_sent_ms) >=
          MECS_CLIENT_HEARTBEAT_MS) {
    mecs_io_encode_heartbeat(client->config.session, &frame);
    if (client->config.send_frame(client->config.transport_context, &frame)) {
      if (client->session_settling && !client->heartbeat_sent) {
        client->session_ready_ms = now_ms + MECS_CLIENT_HEARTBEAT_MS;
      }
      client->heartbeat_sent_ms = now_ms;
      client->heartbeat_sent = true;
    }
  }

  /* Give nodes time to observe the new session heartbeat before sending any
   * queued property commands. The new session intentionally stops outputs. */
  if (client->session_settling) {
    if (!client->heartbeat_sent ||
        (int32_t)(now_ms - client->session_ready_ms) < 0) {
      return;
    }
    client->session_settling = false;
  }

  if (client->discovery_requested || !client->discovery_sent ||
      (uint32_t)(now_ms - client->discovery_sent_ms) >=
          MECS_CLIENT_DISCOVERY_MS) {
    if (mecs_request_discovery(&client->discovery)) {
      client->discovery_requested = false;
      client->discovery_sent = true;
      client->discovery_sent_ms = now_ms;
    }
  }

  /* Start one queued application request after the bus background traffic.
   * Transaction numbers are allocated only when the request is sent. */
  if (!client->request_active && client->queue_count) {
    const mecs_client_queued_request_t next = client->queue[client->queue_head];
    const mecs_client_node_t *node = mecs_client_node(client, next.node);
    if (!mecs_client_node_online(node, now_ms)) {
      emit(client, MECS_CLIENT_EVENT_REQUEST_REJECTED, next.node,
           next.channel, next.property, next.value, MECS_ERR_OFFLINE);
      client->queue_head = (client->queue_head + 1u) % MECS_CLIENT_QUEUE_LENGTH;
      --client->queue_count;
      return;
    }
    if (!node->status.master_alive) {
      return; /* Online is not yet ready: the node has not seen a heartbeat. */
    }
    if (client->next_transaction == UINT16_MAX) {
      return; /* Keep queued work until the application rotates the session. */
    }
    client->queue_head = (client->queue_head + 1u) % MECS_CLIENT_QUEUE_LENGTH;
    --client->queue_count;
    client->request_node = next.node;
    client->request_channel = next.channel;
    client->request_property = next.property;
    client->request_value = next.value;
    client->request = (mecs_io_request_t){
        .property = next.property | (next.read ? MECS_READ_FLAG : 0u),
        .channel = next.channel,
        .transaction = ++client->next_transaction,
        .session = client->config.session,
        .value = next.value,
    };
    client->request_started_ms = now_ms;
    client->request_sent_ms = 0;
    client->request_attempts = 0;
    client->request_active = true;
  }

  if (!client->request_active) {
    return;
  }
  if (expire_request(client, now_ms)) return;
  if (!client->request_attempts ||
      (uint32_t)(now_ms - client->request_sent_ms) >=
          MECS_CLIENT_REQUEST_TIMEOUT_MS) {
    if (mecs_io_encode_request(client->request_node, &client->request, &frame) &&
        client->config.send_frame(client->config.transport_context, &frame)) {
      ++client->request_attempts;
      client->request_sent_ms = now_ms;
    }
  }
}

void mecs_client_receive(mecs_client_t *client,
                        const mecs_frame_t *frame,
                        uint32_t now_ms) {
  if (!client || !frame) {
    return;
  }
  client->now_ms = now_ms;
  (void)expire_request(client, now_ms);
  uint8_t node_id, channel;
  mecs_io_reply_t reply;
  mecs_io_status_t status;
  mecs_pwm_measurement_t measurement;

  if (mecs_io_decode_reply(frame, &node_id, &reply)) {
    if (!client->request_active || !client->request_attempts ||
        node_id != client->request_node ||
        reply.session != client->config.session ||
        reply.transaction != client->request.transaction ||
        reply.property != client->request.property) {
      return;
    }
    if ((reply.error == MECS_ERR_SESSION || reply.error == MECS_ERR_OFFLINE) &&
        client->request_attempts < MECS_CLIENT_MAX_ATTEMPTS) {
      /* These rejections happen before the node executes/caches a command.
       * Repair the lease and retry the SAME transaction, within its original
       * deadline. Do not throw away the rest of a valid setup sequence. */
      client->heartbeat_sent = false;
      return;
    }
    mecs_client_node_t *node = find_node(client, node_id);
    if (reply.error == MECS_OK && node) {
      const unsigned slot = slot_for(client->request_channel);
      const uint8_t property = client->request_property;
      node->value[slot][property] = reply.value;
      node->valid[slot] |= 1u << property;
      emit(client, MECS_CLIENT_EVENT_REQUEST_CONFIRMED, node_id,
           client->request_channel, property, reply.value, MECS_OK);
    } else {
      emit(client, MECS_CLIENT_EVENT_REQUEST_REJECTED, node_id,
           client->request_channel, client->request_property, reply.value,
           reply.error);
    }
    client->request_active = false;
    return;
  }

  if (mecs_io_decode_status(frame, &node_id, &status)) {
    mecs_client_node_t *node = find_node(client, node_id);
    if (!node) {
      client->discovery_requested = true;
      return;
    }
    if (node->status_seen &&
        (node->status.boot_id != status.boot_id ||
         !mecs_client_node_online(node, now_ms) ||
         (node->status.master_alive && !status.master_alive))) {
      invalidate_node(client, node);
    }
    node->status = status;
    node->status_seen = true;
    node->last_status_ms = now_ms;
    if (!status.master_alive) {
      client->heartbeat_sent = false;
    }
    if (status.logical <= 0x0fu && node->identity.board_type == MECS_BOARD_DO4) {
      for (unsigned ch = 0; ch < MECS_CHANNELS; ++ch) {
        node->value[ch][MECS_PROP_VALUE] = (status.logical >> ch) & 1u;
        node->valid[ch] |= 1u << MECS_PROP_VALUE;
      }
    }
    emit(client, MECS_CLIENT_EVENT_NODE_STATUS, node_id, MECS_GLOBAL_CHANNEL,
         0u, status.logical, MECS_OK);
    return;
  }

  if (mecs_io_decode_measurement(frame, &node_id, &channel, &measurement)) {
    mecs_client_node_t *node = find_node(client, node_id);
    if (node && node->identity.board_type == MECS_BOARD_DI4) {
      node->measurement[channel] = measurement;
      node->measurement_ms[channel] = now_ms;
      emit(client, MECS_CLIENT_EVENT_MEASUREMENT, node_id, channel,
           MECS_PROP_FILTER, measurement.valid ? measurement.duty_percent_x100
                                              : 0u,
           MECS_OK);
    }
    return;
  }

  const mecs_rx_result_t result = mecs_receive(&client->discovery, frame);
  if (result == MECS_RX_HANDLED && frame->id > MECS_ANNOUNCE_BASE &&
      frame->id <= MECS_ANNOUNCE_BASE + MECS_MAX_NODE_ID) {
    node_id = (uint8_t)(frame->id - MECS_ANNOUNCE_BASE);
    mecs_client_node_t *node = find_node(client, node_id);
    if (node) {
      node->last_seen_ms = now_ms;
    }
  }
}

static bool submit(mecs_client_t *client, uint8_t node_id, uint8_t channel,
                   mecs_property_t property, uint32_t value, bool read) {
  if (!client || !node_id ||
      node_id > MECS_MAX_NODE_ID ||
      (channel >= MECS_CHANNELS && channel != MECS_GLOBAL_CHANNEL) ||
      (unsigned)property == 0u || (unsigned)property >= MECS_PROP_COUNT) {
    return false;
  }
  mecs_client_node_t *node = find_node(client, node_id);
  if (!mecs_client_node_online(node, client->now_ms)) {
    return false;
  }
  const mecs_property_info_t *info = &mecs_properties[property];
  const uint8_t role = role_for_board(node->identity.board_type);
  const bool global = property == MECS_PROP_REPORT ||
                      property == MECS_PROP_REPORT_MS ||
                      property == MECS_PROP_REPORT_MIN_MS;
  if (!role || !(info->roles & role) || (read && property == MECS_PROP_TRIGGER) ||
      (!read && info->readonly) ||
      global != (channel == MECS_GLOBAL_CHANNEL) ||
      (!read && (value < info->minimum || value > info->maximum))) {
    return false;
  }
  if (client->queue_count >= MECS_CLIENT_QUEUE_LENGTH) {
    return false;
  }
  const unsigned tail = (client->queue_head + client->queue_count) %
                        MECS_CLIENT_QUEUE_LENGTH;
  client->queue[tail] = (mecs_client_queued_request_t){
      .node = node_id,
      .channel = channel,
      .property = (uint8_t)property,
      .value = value,
      .read = read,
  };
  ++client->queue_count;
  return true;
}

bool mecs_client_get(mecs_client_t *client, uint8_t node_id, uint8_t channel,
                    mecs_property_t property) {
  return submit(client, node_id, channel, property, 0u, true);
}

bool mecs_client_set(mecs_client_t *client, uint8_t node_id, uint8_t channel,
                    mecs_property_t property, uint32_t value) {
  return submit(client, node_id, channel, property, value, false);
}

const mecs_client_node_t *mecs_client_node(const mecs_client_t *client,
                                         uint8_t node_id) {
  if (!client) {
    return NULL;
  }
  for (unsigned i = 0; i < MECS_CLIENT_MAX_NODES; ++i) {
    if (client->nodes[i].used &&
        client->nodes[i].identity.node_id == node_id) {
      return &client->nodes[i];
    }
  }
  return NULL;
}

bool mecs_client_node_online(const mecs_client_node_t *node, uint32_t now_ms) {
  return node && node->used && node->status_seen &&
         (uint32_t)(now_ms - node->last_status_ms) < MECS_CLIENT_NODE_ONLINE_MS;
}

bool mecs_client_request_pending(const mecs_client_t *client) {
  return client && (client->request_active || client->queue_count != 0u);
}

bool mecs_client_needs_new_session(const mecs_client_t *client) {
  return client && client->next_transaction == UINT16_MAX &&
         !client->request_active;
}

bool mecs_client_set_session(mecs_client_t *client, uint16_t new_session) {
  if (!client || !new_session || client->request_active ||
      new_session == client->config.session) {
    return false;
  }
  client->config.session = new_session;
  client->next_transaction = 0;
  client->heartbeat_sent = false;
  client->session_settling = true;
  for (unsigned i = 0; i < MECS_CLIENT_MAX_NODES; ++i) {
    if (client->nodes[i].used) {
      invalidate_node(client, &client->nodes[i]);
      client->nodes[i].status.master_alive = false;
    }
  }
  return true;
}
