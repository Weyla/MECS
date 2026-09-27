/* Master state, discovery registry and CAN transaction engine. */
#include "esp_log.h"
#include "esp_timer.h"
#include "master_internal.h"
#include "mecs_can.h"
#include "nvs.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

node_view_t nodes[MASTER_MAX_NODES];
char logs[MASTER_LOG_COUNT][MASTER_LOG_WIDTH];
unsigned log_next, log_used;
uint32_t log_sequence;
SemaphoreHandle_t mutex;
QueueHandle_t commands;
mecs_t discovery;
uint16_t session, transaction;
bool discover_pending;
bool heartbeat_pending;
master_pending_t pending;

/* Persist the master session before use. Consecutive master boots therefore
 * cannot accidentally share a random session and keep output gates active after
 * a master restart. The 16-bit counter skips zero; full wrap is a documented
 * protocol limit. */
uint16_t master_next_session(void) {
  nvs_handle_t handle;
  ESP_ERROR_CHECK(nvs_open("mecs", NVS_READWRITE, &handle));
  uint16_t previous = 0;
  esp_err_t error = nvs_get_u16(handle, "session", &previous);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    /* Read the old MIO namespace once during migration. If a node is still
     * powered, the first new heartbeat must differ from the active session. */
    nvs_handle_t previous_handle;
    const esp_err_t opened = nvs_open("mio", NVS_READONLY, &previous_handle);
    if (opened == ESP_OK) {
      error = nvs_get_u16(previous_handle, "session", &previous);
      nvs_close(previous_handle);
    } else if (opened != ESP_ERR_NVS_NOT_FOUND) {
      error = opened;
    }
  }
  ESP_ERROR_CHECK(error == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : error);
  uint16_t next = previous == UINT16_MAX ? 1 : previous + 1;
  ESP_ERROR_CHECK(nvs_set_u16(handle, "session", next));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);
  return next;
}

uint32_t master_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* Notes are copied into a bounded ring for the UI, and also printed to serial.
 * Wi-Fi passwords never enter this function. Recursive locking permits notes
 * during a registry update while preserving a single locking rule. */
void master_note(const char *format, ...) {
  char text[MASTER_LOG_WIDTH];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  ESP_LOGI("master", "%s", text);
  if (!mutex) {
    return;
  }
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  snprintf(logs[log_next], MASTER_LOG_WIDTH, "%lu ms  %.125s",
           (unsigned long)master_now_ms(), text);
  log_next = (log_next + 1) % MASTER_LOG_COUNT;
  if (log_used < MASTER_LOG_COUNT) {
    ++log_used;
  }
  ++log_sequence;
  xSemaphoreGiveRecursive(mutex);
}
node_view_t *master_lookup(uint8_t address) {
  for (unsigned i = 0; i < MASTER_MAX_NODES; ++i) {
    if (nodes[i].used && nodes[i].identity.node_id == address) {
      return &nodes[i];
    }
  }
  return NULL;
}
bool master_online(const node_view_t *n, uint32_t now) {
  return n && n->status_seen &&
         (uint32_t)(now - n->status_ms) < MASTER_ONLINE_MS;
}
unsigned master_slot(uint8_t channel) {
  return channel == MECS_GLOBAL_CHANNEL ? MECS_CHANNELS : channel;
}

/* The owner holds mutex while handling announcements and status frames, so
 * command producers cannot race this bounded queue rebuild. Preserve commands
 * for every other node when one node restarts. */
static void cancel_node_commands(uint8_t address) {
  if (!commands) return;

  command_t retained[MASTER_COMMAND_QUEUE_LENGTH];
  UBaseType_t retained_count = 0;
  const UBaseType_t queued = uxQueueMessagesWaiting(commands);
  for (UBaseType_t i = 0; i < queued; ++i) {
    command_t command;
    if (xQueueReceive(commands, &command, 0) != pdTRUE) break;
    if (command.node != address) retained[retained_count++] = command;
  }

  xQueueReset(commands);
  for (UBaseType_t i = 0; i < retained_count; ++i) {
    if (xQueueSend(commands, &retained[i], 0) != pdTRUE) {
      master_note("Could not restore queued command for node %u",
                  retained[i].node);
    }
  }
}

void master_discovered(void *context, const mecs_identity_t *identity,
                       mecs_announce_reason_t reason) {
  (void)context;
  node_view_t *n = master_lookup(identity->node_id);
  if (!n) {
    for (unsigned i = 0; i < MASTER_MAX_NODES; ++i) {
      if (!nodes[i].used) {
        n = &nodes[i];
        break;
      }
    }
    if (!n) {
      master_note("Registry full (16 nodes); ignored node %u",
                  identity->node_id);
      return;
    }
  }
  bool reset = !n->used || n->identity.board_type != identity->board_type ||
               reason == MECS_ANNOUNCE_BOOT;
  if (reset) {
    heartbeat_pending = true;
    /* A boot invalidates this node's queued intentions and old readback. */
    if (n->used) {
      cancel_node_commands(identity->node_id);
    }
    memset(n, 0, sizeof(*n));
    n->used = true;
    n->refreshing = true;
    if (pending.active && pending.command.node == identity->node_id) {
      master_note("Node %u rebooted; pending transaction cancelled",
                  identity->node_id);
      pending.active = false;
    }
    master_note("Node %u announced: board=%u firmware=%u.%u.%u",
                identity->node_id, identity->board_type,
                identity->firmware_major, identity->firmware_minor,
                identity->firmware_patch);
  }
  n->identity = *identity;
  n->seen_ms = master_now_ms();
}

bool master_global_property(unsigned property) {
  return property == MECS_PROP_REPORT || property == MECS_PROP_REPORT_MS ||
         property == MECS_PROP_REPORT_MIN_MS;
}
uint8_t master_role_of(const node_view_t *n) {
  return n->identity.board_type == MECS_BOARD_DI4   ? 1
         : n->identity.board_type == MECS_BOARD_DO4 ? 2
                                                   : 0;
}

/* On discovery, read every supported property from the node. These reads have
 * lower priority than queued user actions and use the same transaction engine.
 */
static bool background_read(command_t *out, uint32_t now) {
  for (unsigned i = 0; i < MASTER_MAX_NODES; ++i) {
    node_view_t *n = &nodes[i];
    if (!n->used || !n->refreshing || !master_online(n, now) ||
        !master_role_of(n)) {
      continue;
    }
    while (n->refresh_cursor < (MECS_CHANNELS + 1) * MECS_PROP_COUNT) {
      unsigned index = n->refresh_cursor++;
      unsigned ch = index / MECS_PROP_COUNT, p = index % MECS_PROP_COUNT;
      if (!p || p == MECS_PROP_TRIGGER ||
          !(mecs_properties[p].roles & master_role_of(n)) ||
          master_global_property(p) != (ch == MECS_CHANNELS)) {
        continue;
      }
      *out = (command_t){n->identity.node_id,
                         ch == MECS_CHANNELS ? MECS_GLOBAL_CHANNEL : ch,
                         p | MECS_READ_FLAG, 0};
      return true;
    }
    n->refreshing = false;
  }
  return false;
}

void master_accept_frame(const mecs_frame_t *frame, uint32_t now) {
  uint8_t address;
  mecs_io_reply_t reply;
  mecs_io_status_t status;
  mecs_pwm_measurement_t measurement;
  uint8_t channel;
  if (mecs_io_decode_reply(frame, &address, &reply)) {
    if (!pending.active || address != pending.command.node ||
        reply.session != session ||
        reply.transaction != pending.request.transaction ||
        reply.property != pending.request.property) {
      return;
    }
    node_view_t *n = master_lookup(address);
    unsigned p = reply.property & ~MECS_READ_FLAG;
    if ((reply.error == MECS_ERR_SESSION || reply.error == MECS_ERR_OFFLINE) &&
        pending.attempts < 3) {
      heartbeat_pending = true;
      return; /* Repair the lease, then retry within the original deadline. */
    }
    if (reply.error == MECS_OK && n) {
      n->values[master_slot(pending.command.channel)][p] = reply.value;
      n->valid[master_slot(pending.command.channel)] |= 1u << p;
      /* The two duty properties address the same setting. A legacy CLI write
       * must also update the precise duty shown by the test dashboard. */
      const unsigned channel_slot = master_slot(pending.command.channel);
      if (p == MECS_PROP_DUTY_PRECISE) {
        n->values[channel_slot][MECS_PROP_DUTY] = (reply.value + 5u) / 10u;
        n->valid[channel_slot] |= 1u << MECS_PROP_DUTY;
      } else if (p == MECS_PROP_DUTY && !(reply.property & MECS_READ_FLAG)) {
        n->values[channel_slot][MECS_PROP_DUTY_PRECISE] = reply.value * 10u;
        n->valid[channel_slot] |= 1u << MECS_PROP_DUTY_PRECISE;
      }
      if (pending.user_requested || !(reply.property & MECS_READ_FLAG)) {
        const mecs_property_info_t *info = &mecs_properties[p];
        char value_text[24];
        if (info->scale == 1000) {
          snprintf(value_text, sizeof(value_text), "%lu.%03lu",
                   (unsigned long)(reply.value / 1000),
                   (unsigned long)(reply.value % 1000));
        } else if (info->scale == 100) {
          snprintf(value_text, sizeof(value_text), "%u.%02u", (unsigned)(reply.value / 100),
                   (unsigned)(reply.value % 100));
        } else if (info->scale == 10) {
          snprintf(value_text, sizeof(value_text), "%u.%u", (unsigned)(reply.value / 10),
                   (unsigned)(reply.value % 10));
        } else {
          snprintf(value_text, sizeof(value_text), "%u", (unsigned)reply.value);
        }
        master_note("Confirmed node %u channel %u %s=%s %s", address,
                    pending.command.channel, info->name, value_text,
                    info->unit);
      }
    } else {
      master_note("Node %u rejected %s: %s", address, mecs_properties[p].name,
                  mecs_error_name(reply.error));
    }
    pending.active = false;
  } else if (mecs_io_decode_status(frame, &address, &status)) {
    node_view_t *n = master_lookup(address);
    if (!n) {
      discover_pending = true;
      return;
    }
    if (n->status_seen && n->status.boot_id != status.boot_id) {
      cancel_node_commands(address);
      memset(n->valid, 0, sizeof(n->valid));
      memset(n->measurement, 0, sizeof(n->measurement));
      n->refresh_cursor = 0;
      n->refreshing = true;
      if (pending.active && pending.command.node == address) {
        pending.active = false;
      }
      master_note("Node %u boot identity changed; refreshing settings",
                  address);
    }
    bool was_online = master_online(n, now);
    n->status = status;
    n->status_seen = true;
    n->status_ms = now;
    if (!status.master_alive) {
      heartbeat_pending = true;
    }
    if (!was_online) {
      n->refreshing = true;
      n->refresh_cursor = 0;
      master_note("Node %u online", address);
    }
    /* Volatile state is authoritative in status, not an old command ACK. */
    if (n->identity.board_type == MECS_BOARD_DO4) {
      for (unsigned ch = 0; ch < MECS_CHANNELS; ++ch) {
        n->values[ch][MECS_PROP_VALUE] = (status.logical >> ch) & 1;
        n->valid[ch] |= 1u << MECS_PROP_VALUE;
      }
    }
  } else if (mecs_io_decode_measurement(frame, &address, &channel,
                                       &measurement)) {
    node_view_t *n = master_lookup(address);
    if (n && n->identity.board_type == MECS_BOARD_DI4) {
      n->measurement[channel] = measurement;
      n->measurement_ms[channel] = now;
    }
  } else {
    (void)mecs_receive(&discovery, frame);
  }
}

void master_transactions(uint32_t now) {
  if (!pending.active) {
    command_t command;
    bool user_requested = xQueueReceive(commands, &command, 0) == pdTRUE;
    if (!user_requested && !background_read(&command, now)) {
      return;
    }
    node_view_t *n = master_lookup(command.node);
    if (!master_online(n, now)) {
      master_note("Node %u offline; command not sent", command.node);
      return;
    }
    if (transaction == UINT16_MAX) {
      /* Never reuse a transaction within a session. Rollover stops
       * nodes when the new heartbeat arrives and requires explicit channel
       * enable. */
      session = master_next_session();
      transaction = 0;
      xQueueReset(commands);
      master_note("Session rotated; outputs will require explicit enabling");
      return;
    }
    pending.user_requested = user_requested;
    pending.command = command;
    pending.request = (mecs_io_request_t){command.property, command.channel,
                                         ++transaction, session, command.value};
    pending.active = true;
    pending.attempts = 0;
    pending.started_ms = now;
    pending.sent_ms = 0;
  }
  if ((uint32_t)(now - pending.started_ms) >= 1500 ||
      (pending.attempts >= 3 &&
       (uint32_t)(now - pending.sent_ms) >= MASTER_COMMAND_TIMEOUT_MS)) {
    master_note("Node %u transaction %u timed out; outcome unknown, refresh "
                "before retrying actions",
                pending.command.node, pending.request.transaction);
    node_view_t *n = master_lookup(pending.command.node);
    if (n) {
      n->valid[master_slot(pending.command.channel)] &=
          ~(1u << (pending.command.property & ~MECS_READ_FLAG));
    }
    /* Do not run later queued output actions after an uncertain result. */
    xQueueReset(commands);
    pending.active = false;
    return;
  }
  if (!pending.attempts ||
      (uint32_t)(now - pending.sent_ms) >= MASTER_COMMAND_TIMEOUT_MS) {
    mecs_frame_t frame;
    if (mecs_io_encode_request(pending.command.node, &pending.request, &frame) &&
        mecs_can_send(NULL, &frame)) {
      ++pending.attempts;
      pending.sent_ms = now;
    }
  }
}
