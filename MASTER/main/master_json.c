/* Bounded JSON snapshot for the dashboard API. */
#include "master_internal.h"
#include "mio_command.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Small bounded JSON writer: only fixed property names, integers and escaped
 * logs are emitted. A failed capacity check produces no partial JSON response.
 */
typedef struct {
  char *data;
  size_t used, capacity;
  bool ok;
} json_t;
static void add(json_t *j, const char *format, ...) {
  if (!j->ok) {
    return;
  }
  va_list args;
  va_start(args, format);
  int count = vsnprintf(j->data + j->used, j->capacity - j->used, format, args);
  va_end(args);
  if (count < 0 || (size_t)count >= j->capacity - j->used) {
    j->ok = false;
    return;
  }
  j->used += count;
}
static void quoted(json_t *j, const char *s) {
  add(j, "\"");
  for (; *s; ++s) {
    unsigned char c = *s;
    if (c == '"' || c == '\\') {
      add(j, "\\%c", c);
    } else if (c < 32) {
      add(j, "\\u%04x", c);
    } else {
      add(j, "%c", c);
    }
  }
  add(j, "\"");
}
char *master_json(void) {
  json_t j = {.capacity = 49152, .ok = true};
  j.data = malloc(j.capacity);
  if (!j.data) {
    return NULL;
  }
  char ip[32];
  network_address(ip, sizeof(ip));
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  uint32_t now = master_now_ms();
  add(&j,
      "{\"uptime_ms\":%lu,\"session\":%u,\"pending\":%s,\"queued\":%u,\"ip\":",
      (unsigned long)now, session, pending.active ? "true" : "false",
      (unsigned)uxQueueMessagesWaiting(commands));
  quoted(&j, ip);
  add(&j, ",\"schema\":[");
  for (unsigned p = 1; p < MIO_PROP_COUNT; ++p) {
    const mio_property_info_t *m = &mio_properties[p];
    add(&j,
        "%s{\"id\":%u,\"name\":\"%s\",\"label\":\"%s\",\"unit\":\"%s\","
        "\"choices\":\"%s\","
        "\"min\":%u,\"max\":%u,\"roles\":%u,\"readonly\":%s,\"global\":%s,"
        "\"scale\":%u}",
        p == 1 ? "" : ",", p, m->name, m->label, m->unit, m->choices,
        m->minimum, m->maximum, m->roles, m->readonly ? "true" : "false",
        master_global_property(p) ? "true" : "false", m->scale);
  }
  add(&j, "],\"nodes\":[");
  bool first = true;
  for (unsigned i = 0; i < MASTER_MAX_NODES; ++i) {
    node_view_t *n = &nodes[i];
    if (!n->used) {
      continue;
    }
    add(&j,
        "%s{\"id\":%u,\"board\":%u,\"role\":%u,\"firmware\":\"%u.%u.%u\","
        "\"online\":%s,\"age_"
        "ms\":%lu,\"raw\":%u,\"logical\":%u,\"master_alive\":%s,\"fault\":%s,"
        "\"refreshing\":%s,\"values\":[",
        first ? "" : ",", n->identity.node_id, n->identity.board_type,
        master_role_of(n), n->identity.firmware_major,
        n->identity.firmware_minor, n->identity.firmware_patch,
        master_online(n, now) ? "true" : "false",
        (unsigned long)(n->status_seen ? now - n->status_ms : now - n->seen_ms),
        n->status.raw, n->status.logical,
        n->status.master_alive ? "true" : "false",
        n->status.fault ? "true" : "false", n->refreshing ? "true" : "false");
    first = false;
    for (unsigned ch = 0; ch <= MIO_CHANNELS; ++ch) {
      add(&j, "%s{", ch ? "," : "");
      bool first_prop = true;
      for (unsigned p = 1; p < MIO_PROP_COUNT; ++p) {
        if (n->valid[ch] & (1u << p)) {
          add(&j, "%s\"%s\":%u", first_prop ? "" : ",", mio_properties[p].name,
              n->values[ch][p]);
          first_prop = false;
        }
      }
      add(&j, "}");
    }
    add(&j, "],\"measurements\":[");
    for (unsigned ch = 0; ch < MIO_CHANNELS; ++ch) {
      const mio_pwm_measurement_t *m = &n->measurement[ch];
      bool fresh = master_online(n, now) &&
                   (uint32_t)(now - n->measurement_ms[ch]) < 1500 &&
                   (n->valid[ch] & (1u << MIO_PROP_FILTER)) &&
                   n->values[ch][MIO_PROP_FILTER] == MIO_FILTER_PWM;
      add(&j, "%s{\"valid\":%s,\"period_us\":%lu,\"duty\":%u}", ch ? "," : "",
          fresh && m->valid ? "true" : "false", (unsigned long)m->period_us,
          m->duty_percent_x100);
    }
    add(&j, "]}");
  }
  add(&j, "],\"log_sequence\":%lu,\"logs\":[", (unsigned long)log_sequence);
  for (unsigned i = 0; i < log_used; ++i) {
    if (i) {
      add(&j, ",");
    }
    quoted(
        &j,
        logs[(log_next + MASTER_LOG_COUNT - log_used + i) % MASTER_LOG_COUNT]);
  }
  add(&j, "]}");
  xSemaphoreGiveRecursive(mutex);
  if (!j.ok) {
    free(j.data);
    return NULL;
  }
  return j.data;
}
