/* Revision 3 I/O frames use explicit little-endian words, never C struct
 * layout. Configuration writes have a session and transaction number so retries
 * can be acknowledged without repeating side effects such as starting a timed
 * pulse. */
#include "mio_io.h"

static uint16_t word(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static void put(uint8_t *p, uint16_t n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
}
static bool valid(const mio_frame_t *f, uint32_t id, uint8_t length) {
  return f && !f->extended && !f->remote && f->id == id && f->length == length;
}
static bool source(const mio_frame_t *f, uint32_t base, uint8_t *node) {
  if (!f || !node || f->id <= base || f->id > base + MIO_MAX_NODE_ID ||
      !valid(f, f->id, 8)) {
    return false;
  }
  *node = (uint8_t)(f->id - base);
  return true;
}

bool mio_io_encode_request(uint8_t node, const mio_io_request_t *r,
                           mio_frame_t *f) {
  if (!r || !f || !node || node > MIO_MAX_NODE_ID || !r->session ||
      !r->transaction) {
    return false;
  }
  *f = (mio_frame_t){.id = MIO_COMMAND_BASE + node, .length = 8};
  f->data[0] = r->property;
  f->data[1] = r->channel;
  put(f->data + 2, r->transaction);
  put(f->data + 4, r->session);
  put(f->data + 6, r->value);
  return true;
}
bool mio_io_decode_request(uint8_t node, const mio_frame_t *f,
                           mio_io_request_t *r) {
  /* Compare the destination CAN ID before reading the command payload. Each
   * node calls this with its own address, so other nodes' commands are ignored.
   */
  if (!r || !node || node > MIO_MAX_NODE_ID ||
      !valid(f, MIO_COMMAND_BASE + node, 8)) {
    return false;
  }
  *r = (mio_io_request_t){f->data[0], f->data[1], word(f->data + 2),
                          word(f->data + 4), word(f->data + 6)};
  return r->session && r->transaction;
}
void mio_io_encode_reply(uint8_t node, const mio_io_reply_t *r,
                         mio_frame_t *f) {
  *f = (mio_frame_t){.id = MIO_REPLY_BASE + node, .length = 8};
  f->data[0] = r->property;
  f->data[1] = (uint8_t)r->error;
  put(f->data + 2, r->transaction);
  put(f->data + 4, r->session);
  put(f->data + 6, r->value);
}
bool mio_io_decode_reply(const mio_frame_t *f, uint8_t *node,
                         mio_io_reply_t *r) {
  if (!r || !source(f, MIO_REPLY_BASE, node) || f->data[1] > MIO_ERR_SEQUENCE) {
    return false;
  }
  *r = (mio_io_reply_t){f->data[0], (mio_error_t)f->data[1], word(f->data + 2),
                        word(f->data + 4), word(f->data + 6)};
  return r->session && r->transaction;
}
void mio_io_encode_heartbeat(uint16_t session, mio_frame_t *f) {
  *f = (mio_frame_t){
      .id = MIO_HEARTBEAT_ID, .length = 3, .data = {MIO_PROTOCOL_VERSION}};
  put(f->data + 1, session);
}
bool mio_io_decode_heartbeat(const mio_frame_t *f, uint16_t *session) {
  if (!session || !valid(f, MIO_HEARTBEAT_ID, 3) ||
      f->data[0] != MIO_PROTOCOL_VERSION) {
    return false;
  }
  *session = word(f->data + 1);
  return *session != 0;
}
void mio_io_encode_status(uint8_t node, const mio_io_status_t *s,
                          mio_frame_t *f) {
  *f = (mio_frame_t){
      .id = MIO_STATUS_BASE + node,
      .length = 8,
      .data = {MIO_PROTOCOL_VERSION,
               (uint8_t)((s->master_alive << 1) | (s->fault << 2)), s->raw,
               s->logical}};
  put(f->data + 4, s->boot_id);
  put(f->data + 6, s->sequence);
}
bool mio_io_decode_status(const mio_frame_t *f, uint8_t *node,
                          mio_io_status_t *s) {
  if (!s || !source(f, MIO_STATUS_BASE, node) ||
      f->data[0] != MIO_PROTOCOL_VERSION || (f->data[1] & 0xf9) ||
      (f->data[2] & 0xf0) || (f->data[3] & 0xf0)) {
    return false;
  }
  *s = (mio_io_status_t){.raw = f->data[2],
                         .logical = f->data[3],
                         .master_alive = (f->data[1] & 2) != 0,
                         .fault = (f->data[1] & 4) != 0,
                         .boot_id = word(f->data + 4),
                         .sequence = word(f->data + 6)};
  return true;
}

/* One telemetry frame per measured channel. Invalid reports carry zero data;
 * bit 7 indicates validity and bits 0..1 select channel, all other bits zero.
 */
void mio_io_encode_measurement(uint8_t node, uint8_t channel,
                               const mio_pwm_measurement_t *m, mio_frame_t *f) {
  *f = (mio_frame_t){.id = MIO_MEASUREMENT_BASE + node,
                     .length = 8,
                     .data = {MIO_PROTOCOL_VERSION,
                              (uint8_t)(channel | (m->valid ? 0x80 : 0))}};
  if (m->valid) {
    put(f->data + 2, (uint16_t)m->period_us);
    put(f->data + 4, (uint16_t)(m->period_us >> 16));
    put(f->data + 6, m->duty_percent_x100);
  }
}
bool mio_io_decode_measurement(const mio_frame_t *f, uint8_t *node,
                               uint8_t *channel, mio_pwm_measurement_t *m) {
  if (!channel || !m || !source(f, MIO_MEASUREMENT_BASE, node) ||
      f->data[0] != MIO_PROTOCOL_VERSION || (f->data[1] & 0x7c)) {
    return false;
  }
  *channel = f->data[1] & 3;
  *m = (mio_pwm_measurement_t){.valid = (f->data[1] & 0x80) != 0,
                               .period_us = (uint32_t)word(f->data + 2) |
                                            ((uint32_t)word(f->data + 4) << 16),
                               .duty_percent_x100 = word(f->data + 6)};
  if (!m->valid) {
    return m->period_us == 0 && m->duty_percent_x100 == 0;
  }
  return m->period_us >= MIO_PWM_MIN_PERIOD_US &&
         m->period_us <= MIO_PWM_MAX_PERIOD_US && m->duty_percent_x100 <= 10000;
}
