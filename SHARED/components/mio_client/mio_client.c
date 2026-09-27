/* Framework-independent master client. Keep transport work in the caller's
 * loop so the same implementation can run from ESP-IDF, Arduino or ESPHome. */
#include "mio_client.h"

#include <string.h>

#define MIO_CLIENT_HEARTBEAT_MS 250u
#define MIO_CLIENT_DISCOVERY_MS 5000u
#define MIO_CLIENT_MAX_ATTEMPTS 3u
#define MIO_CLIENT_REQUEST_LIFETIME_MS 1500u

static unsigned slot_for(uint8_t channel) {
  return channel == MIO_GLOBAL_CHANNEL ? MIO_CHANNELS : channel;
}

static uint8_t role_for_board(uint16_t board_type) {
  if (board_type == MIO_BOARD_DI4) {
    return 1u;
  }
  if (board_type == MIO_BOARD_DO4) {
    return 2u;
  }
  return 0u;
}

static mio_client_node_t *find_node(mio_client_t *client, uint8_t node_id) {
  for (unsigned i = 0; i < MIO_CLIENT_MAX_NODES; ++i) {
    if (client->nodes[i].used &&
        client->nodes[i].identity.node_id == node_id) {
      return &client->nodes[i];
    }
  }
  return NULL;
}

static void emit(mio_client_t *client, mio_client_event_t event,
                 uint8_t node, uint8_t channel, uint8_t property,
                 uint16_t value, mio_error_t error) {
  if (client->on_event) {
    client->on_event(client->event_context, event, node, channel,
                     property, value, error);
  }
}

/* mio_t shares one callback context between transport and discovery. This
 * small trampoline keeps transport ownership in the public client config. */
static bool client_send(void *context, const mio_frame_t *frame) {
  mio_client_t *client = context;
  return client->config.send_frame(client->config.transport_context, frame);
}

static void discovered(void *context, const mio_identity_t *identity,
                       mio_announce_reason_t reason) {
  mio_client_t *client = context;
  mio_client_node_t *node = find_node(client, identity->node_id);
  if (!node) {
    for (unsigned i = 0; i < MIO_CLIENT_MAX_NODES; ++i) {
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
      (reason == MIO_ANNOUNCE_BOOT ||
       node->identity.board_type != identity->board_type);
  if (!node->used || rebooted) {
    memset(node, 0, sizeof(*node));
  }
  node->used = true;
  node->identity = *identity;
  emit(client, MIO_CLIENT_EVENT_NODE_ANNOUNCED, identity->node_id,
       MIO_GLOBAL_CHANNEL, 0u, identity->board_type, MIO_OK);
}

bool mio_client_begin(mio_client_t *client,
                      const mio_client_config_t *config,
                      mio_client_event_fn on_event,
                      void *event_context) {
  if (!client || !config || !config->send_frame || !config->session) {
    return false;
  }
  memset(client, 0, sizeof(*client));
  client->config = *config;
  client->on_event = on_event;
  client->event_context = event_context;
  const mio_identity_t master_identity = {0};
  return mio_init(&client->discovery, master_identity, client_send, discovered,
                  client);
}

bool mio_client_discover(mio_client_t *client) {
  if (!client || !mio_request_discovery(&client->discovery)) {
    return false;
  }
  client->discovery_requested = false;
  client->discovery_sent = true;
  client->discovery_sent_ms = client->now_ms;
  return true;
}

void mio_client_loop(mio_client_t *client, uint32_t now_ms) {
  if (!client || !client->config.send_frame) {
    return;
  }
  client->now_ms = now_ms;

  mio_frame_t frame;
  if (!client->heartbeat_sent ||
      (uint32_t)(now_ms - client->heartbeat_sent_ms) >=
          MIO_CLIENT_HEARTBEAT_MS) {
    mio_io_encode_heartbeat(client->config.session, &frame);
    if (client->config.send_frame(client->config.transport_context, &frame)) {
      client->heartbeat_sent_ms = now_ms;
      client->heartbeat_sent = true;
      if (client->session_settling) {
        client->session_ready_ms = now_ms + MIO_CLIENT_HEARTBEAT_MS;
      }
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
          MIO_CLIENT_DISCOVERY_MS) {
    if (mio_request_discovery(&client->discovery)) {
      client->discovery_requested = false;
      client->discovery_sent = true;
      client->discovery_sent_ms = now_ms;
    }
  }

  /* Start one queued application request after the bus background traffic.
   * Transaction numbers are allocated only when the request is sent. */
  if (!client->request_active && client->queue_count) {
    const mio_client_queued_request_t next = client->queue[client->queue_head];
    const mio_client_node_t *node = mio_client_node(client, next.node);
    if (!mio_client_node_online(node, now_ms)) {
      emit(client, MIO_CLIENT_EVENT_REQUEST_REJECTED, next.node,
           next.channel, next.property, next.value, MIO_ERR_OFFLINE);
      client->queue_head = (client->queue_head + 1u) % MIO_CLIENT_QUEUE_LENGTH;
      --client->queue_count;
      return;
    }
    if (client->next_transaction == UINT16_MAX) {
      return; /* Keep queued work until the application rotates the session. */
    }
    client->queue_head = (client->queue_head + 1u) % MIO_CLIENT_QUEUE_LENGTH;
    --client->queue_count;
    client->request_node = next.node;
    client->request_channel = next.channel;
    client->request_property = next.property;
    client->request_value = next.value;
    client->request = (mio_io_request_t){
        .property = next.property | (next.read ? MIO_READ_FLAG : 0u),
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
  if (client->request_attempts &&
      (uint32_t)(now_ms - client->request_started_ms) >=
      MIO_CLIENT_REQUEST_LIFETIME_MS) {
    emit(client, MIO_CLIENT_EVENT_REQUEST_TIMEOUT, client->request_node,
         client->request_channel, client->request_property,
         client->request_value, MIO_ERR_OFFLINE);
    client->request_active = false;
    return;
  }
  if (client->request_attempts >= MIO_CLIENT_MAX_ATTEMPTS &&
      (uint32_t)(now_ms - client->request_sent_ms) >=
          MIO_CLIENT_REQUEST_TIMEOUT_MS) {
    emit(client, MIO_CLIENT_EVENT_REQUEST_TIMEOUT, client->request_node,
         client->request_channel, client->request_property,
         client->request_value, MIO_ERR_OFFLINE);
    client->request_active = false;
    return;
  }
  if (!client->request_attempts ||
      (uint32_t)(now_ms - client->request_sent_ms) >=
          MIO_CLIENT_REQUEST_TIMEOUT_MS) {
    if (mio_io_encode_request(client->request_node, &client->request, &frame) &&
        client->config.send_frame(client->config.transport_context, &frame)) {
      if (!client->request_attempts) {
        client->request_started_ms = now_ms;
      }
      ++client->request_attempts;
      client->request_sent_ms = now_ms;
    }
  }
}

void mio_client_receive(mio_client_t *client,
                        const mio_frame_t *frame,
                        uint32_t now_ms) {
  if (!client || !frame) {
    return;
  }
  client->now_ms = now_ms;
  uint8_t node_id, channel;
  mio_io_reply_t reply;
  mio_io_status_t status;
  mio_pwm_measurement_t measurement;

  if (mio_io_decode_reply(frame, &node_id, &reply)) {
    if (!client->request_active || node_id != client->request_node ||
        reply.session != client->config.session ||
        reply.transaction != client->request.transaction ||
        reply.property != client->request.property) {
      return;
    }
    mio_client_node_t *node = find_node(client, node_id);
    if (reply.error == MIO_OK && node) {
      const unsigned slot = slot_for(client->request_channel);
      const uint8_t property = client->request_property;
      node->value[slot][property] = reply.value;
      node->valid[slot] |= 1u << property;
      emit(client, MIO_CLIENT_EVENT_REQUEST_CONFIRMED, node_id,
           client->request_channel, property, reply.value, MIO_OK);
    } else {
      emit(client, MIO_CLIENT_EVENT_REQUEST_REJECTED, node_id,
           client->request_channel, client->request_property, reply.value,
           reply.error);
    }
    client->request_active = false;
    return;
  }

  if (mio_io_decode_status(frame, &node_id, &status)) {
    mio_client_node_t *node = find_node(client, node_id);
    if (!node) {
      client->discovery_requested = true;
      return;
    }
    if (node->status_seen && node->status.boot_id != status.boot_id) {
      memset(node->valid, 0, sizeof(node->valid));
      memset(node->measurement, 0, sizeof(node->measurement));
    }
    node->status = status;
    node->status_seen = true;
    node->last_status_ms = now_ms;
    if (status.logical <= 0x0fu && node->identity.board_type == MIO_BOARD_DO4) {
      for (unsigned ch = 0; ch < MIO_CHANNELS; ++ch) {
        node->value[ch][MIO_PROP_VALUE] = (status.logical >> ch) & 1u;
        node->valid[ch] |= 1u << MIO_PROP_VALUE;
      }
    }
    emit(client, MIO_CLIENT_EVENT_NODE_STATUS, node_id, MIO_GLOBAL_CHANNEL,
         0u, status.logical, MIO_OK);
    return;
  }

  if (mio_io_decode_measurement(frame, &node_id, &channel, &measurement)) {
    mio_client_node_t *node = find_node(client, node_id);
    if (node && node->identity.board_type == MIO_BOARD_DI4) {
      node->measurement[channel] = measurement;
      node->measurement_ms[channel] = now_ms;
      emit(client, MIO_CLIENT_EVENT_MEASUREMENT, node_id, channel,
           MIO_PROP_FILTER, measurement.valid ? measurement.duty_percent_x100
                                              : 0u,
           MIO_OK);
    }
    return;
  }

  const mio_rx_result_t result = mio_receive(&client->discovery, frame);
  if (result == MIO_RX_HANDLED && frame->id > MIO_ANNOUNCE_BASE &&
      frame->id <= MIO_ANNOUNCE_BASE + MIO_MAX_NODE_ID) {
    node_id = (uint8_t)(frame->id - MIO_ANNOUNCE_BASE);
    mio_client_node_t *node = find_node(client, node_id);
    if (node) {
      node->last_seen_ms = now_ms;
    }
  }
}

static bool submit(mio_client_t *client, uint8_t node_id, uint8_t channel,
                   mio_property_t property, uint16_t value, bool read) {
  if (!client || !node_id ||
      node_id > MIO_MAX_NODE_ID ||
      (channel >= MIO_CHANNELS && channel != MIO_GLOBAL_CHANNEL) ||
      (unsigned)property == 0u || (unsigned)property >= MIO_PROP_COUNT) {
    return false;
  }
  mio_client_node_t *node = find_node(client, node_id);
  if (!mio_client_node_online(node, client->now_ms)) {
    return false;
  }
  const mio_property_info_t *info = &mio_properties[property];
  const uint8_t role = role_for_board(node->identity.board_type);
  const bool global = property == MIO_PROP_REPORT ||
                      property == MIO_PROP_REPORT_MS;
  if (!role || !(info->roles & role) || (read && property == MIO_PROP_TRIGGER) ||
      (!read && info->readonly) ||
      global != (channel == MIO_GLOBAL_CHANNEL) ||
      (!read && (value < info->minimum || value > info->maximum))) {
    return false;
  }
  if (client->queue_count >= MIO_CLIENT_QUEUE_LENGTH) {
    return false;
  }
  const unsigned tail = (client->queue_head + client->queue_count) %
                        MIO_CLIENT_QUEUE_LENGTH;
  client->queue[tail] = (mio_client_queued_request_t){
      .node = node_id,
      .channel = channel,
      .property = (uint8_t)property,
      .value = value,
      .read = read,
  };
  ++client->queue_count;
  return true;
}

bool mio_client_get(mio_client_t *client, uint8_t node_id, uint8_t channel,
                    mio_property_t property) {
  return submit(client, node_id, channel, property, 0u, true);
}

bool mio_client_set(mio_client_t *client, uint8_t node_id, uint8_t channel,
                    mio_property_t property, uint16_t value) {
  return submit(client, node_id, channel, property, value, false);
}

const mio_client_node_t *mio_client_node(const mio_client_t *client,
                                         uint8_t node_id) {
  if (!client) {
    return NULL;
  }
  for (unsigned i = 0; i < MIO_CLIENT_MAX_NODES; ++i) {
    if (client->nodes[i].used &&
        client->nodes[i].identity.node_id == node_id) {
      return &client->nodes[i];
    }
  }
  return NULL;
}

bool mio_client_node_online(const mio_client_node_t *node, uint32_t now_ms) {
  return node && node->used && node->status_seen &&
         (uint32_t)(now_ms - node->last_status_ms) < MIO_CLIENT_NODE_ONLINE_MS;
}

bool mio_client_request_pending(const mio_client_t *client) {
  return client && (client->request_active || client->queue_count != 0u);
}

bool mio_client_needs_new_session(const mio_client_t *client) {
  return client && client->next_transaction == UINT16_MAX &&
         !client->request_active;
}

bool mio_client_set_session(mio_client_t *client, uint16_t new_session) {
  if (!client || !new_session || client->request_active ||
      new_session == client->config.session) {
    return false;
  }
  client->config.session = new_session;
  client->next_transaction = 0;
  client->heartbeat_sent = false;
  client->session_settling = true;
  return true;
}
