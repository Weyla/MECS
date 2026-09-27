/* Node-side processing: filtering and pulse timing happen locally, independent
 * of the CAN reporting rate. Hardware access is injected through apply().
 * All functions run in a single owner task; no shared ISR state is used here.
 */
#include "mio_io.h"
#include <string.h>

/* Integer fixed-point units are shared across CAN, console and browser.
 * Human-facing duty is percent (two decimals); slow period is seconds (one).
 * There is no binary floating-point conversion in the wire protocol. */
const mio_property_info_t mio_properties[MIO_PROP_COUNT] = {
    [MIO_PROP_POLARITY] = {"polarity", "Active level", "", "High|Low", 0, 1, 3,
                           false, 1},
    [MIO_PROP_FILTER] = {"filter", "Input function", "",
                         "Digital / no filter|Stable debounce|PWM measurement",
                         0, 2, 1, false, 1},
    [MIO_PROP_FILTER_MS] = {"filter_ms", "Debounce time", "ms", "", 1, 1000, 1,
                            false, 1},
    [MIO_PROP_PULL] = {"pull", "Input bias", "", "Floating|Pull-up|Pull-down",
                       0, 2, 1, false, 1},
    [MIO_PROP_REPORT] = {"report", "Reporting", "",
                         "Periodic|On change|Periodic + change", 0, 2, 3, false,
                         1},
    [MIO_PROP_REPORT_MS] = {"report_ms", "Periodic report interval", "ms", "",
                            20, 500, 3, false, 1},
    [MIO_PROP_MODE] = {"mode", "Output mode", "",
                       "Digital|PWM (frequency)|Timed pulse|Slow PWM (period)",
                       0, 3, 2, false, 1},
    [MIO_PROP_FREQUENCY] = {"frequency", "PWM frequency", "Hz", "", 10, 10000,
                            2, false, 1},
    [MIO_PROP_DUTY] = {"duty", "PWM active duty", "%", "", 0, 10000, 2, false,
                       100},
    [MIO_PROP_VALUE] = {"value", "Output enabled", "", "Off|On", 0, 1, 2, false,
                        1},
    [MIO_PROP_PULSE_MS] = {"pulse_ms", "Pulse duration", "ms", "", 10, 60000, 2,
                           false, 1},
    [MIO_PROP_TRIGGER] = {"trigger", "Start pulse", "", "", 1, 1, 2, false, 1},
    [MIO_PROP_PIN] = {"pin", "GPIO", "", "", 0, 21, 3, true, 1},
    [MIO_PROP_PERIOD] = {"period", "Slow PWM period", "s", "", 1, 36000, 2,
                         false, 10},
};

const char *mio_error_name(mio_error_t error) {
  static const char *names[] = {"ok",
                                "unsupported property",
                                "invalid channel",
                                "out of range",
                                "master heartbeat missing",
                                "hardware apply failed",
                                "wrong master session",
                                "stale or reused transaction"};
  return (unsigned)error < sizeof(names) / sizeof(names[0]) ? names[error]
                                                            : "unknown error";
}

void mio_io_node_init(mio_io_node_t *n, uint16_t board,
                      const uint8_t pins[MIO_CHANNELS], bool output_active_low,
                      mio_io_apply_fn apply, void *context) {
  memset(n, 0, sizeof(*n));
  n->board_type = board;
  n->apply = apply;
  n->context = context;
  n->report = MIO_REPORT_BOTH;
  n->report_ms = 100;
  for (unsigned i = 0; i < MIO_CHANNELS; ++i) {
    n->config[i] = (mio_channel_config_t){
        .active_low = board == MIO_BOARD_DI4 || output_active_low,
        .filter = MIO_FILTER_STABLE,
        .filter_ms = 20,
        .pull = MIO_PULL_UP,
        .frequency_hz = 1000,
        .duty_percent_x100 = 5000,
        .period_deciseconds = 100,
        .pulse_ms = 100,
        .pin = pins[i]};
  }
}

/* Communication loss or a hardware fault clears individual enable gates and
 * pulses. Receiving heartbeats again never automatically restarts an output. */
void mio_io_node_stop(mio_io_node_t *n) {
  for (unsigned i = 0; i < MIO_CHANNELS; ++i) {
    n->config[i].value = false;
    n->state[i].pulse_until_ms = 0;
    n->state[i].output_active = false;
    if (n->board_type == MIO_BOARD_DO4 && n->apply &&
        !n->apply(n->context, i, &n->config[i], false)) {
      n->fault = true;
    }
  }
}

void mio_io_node_heartbeat(mio_io_node_t *n, uint16_t session, uint32_t now) {
  if (!session) {
    return;
  }
  /* A newly booted master has a new session. Reset transaction history and
   * require new per-channel enable commands even if the previous heartbeat was
   * recent. */
  if (session != n->session) {
    mio_io_node_stop(n);
    n->cached = false;
    n->session = session;
  }
  n->master_alive = true;
  n->heartbeat_ms = now;
}

void mio_io_node_tick(mio_io_node_t *n, uint8_t raw, uint32_t now) {
  if (n->master_alive && (uint32_t)(now - n->heartbeat_ms) >= MIO_LEASE_MS) {
    n->master_alive = false;
    mio_io_node_stop(n);
  }
  for (unsigned i = 0; i < MIO_CHANNELS; ++i) {
    mio_channel_config_t *c = &n->config[i];
    mio_channel_state_t *s = &n->state[i];
    s->raw = (raw & (1u << i)) != 0;
    if (n->board_type == MIO_BOARD_DO4) {
      if (c->mode == MIO_OUTPUT_PULSE && c->value &&
          (int32_t)(now - s->pulse_until_ms) >= 0) {
        c->value = false;
      }
      bool active = c->value;
      if (c->mode == MIO_OUTPUT_SLOW_PWM && active) {
        uint32_t period_ms = (uint32_t)c->period_deciseconds * 100;
        uint32_t elapsed = now - s->cycle_since_ms;
        /* Advance by complete periods, retaining phase without drift.
         * The 64-bit product avoids overflow at a one-hour period. */
        if (elapsed >= period_ms) {
          s->cycle_since_ms += (elapsed / period_ms) * period_ms;
          elapsed %= period_ms;
        }
        uint32_t active_ms =
            ((uint64_t)period_ms * c->duty_percent_x100) / 10000;
        active = elapsed < active_ms;
      }
      if (active != s->output_active) {
        s->output_active = active;
        if (n->apply && !n->apply(n->context, i, c, active)) {
          n->fault = true;
          mio_io_node_stop(n);
        }
      }
      continue;
    }
    bool active = s->raw != c->active_low;
    if (active != s->candidate) {
      s->candidate = active;
      s->candidate_since_ms = now;
    }
    switch (c->filter) {
    case MIO_FILTER_NONE:
      s->filtered = active;
      break;
    case MIO_FILTER_STABLE:
      if ((uint32_t)(now - s->candidate_since_ms) >= c->filter_ms) {
        s->filtered = active;
      }
      break;
    case MIO_FILTER_PWM:
      /* Raw logical level remains visible. Period/duty are measured by
       * edge capture and reported separately, never by this 1 ms sampler. */
      s->filtered = active;
      break;
    }
  }
}

uint8_t mio_io_node_logical(const mio_io_node_t *n) {
  uint8_t bits = 0;
  for (unsigned i = 0; i < MIO_CHANNELS; ++i) {
    bool active = n->board_type == MIO_BOARD_DI4 ? n->state[i].filtered
                                                 : n->config[i].value;
    if (active) {
      bits |= (uint8_t)(1u << i);
    }
  }
  return bits;
}

static uint16_t read_property(const mio_io_node_t *n, uint8_t channel,
                              uint8_t property) {
  if (property == MIO_PROP_REPORT) {
    return n->report;
  }
  if (property == MIO_PROP_REPORT_MS) {
    return n->report_ms;
  }
  const mio_channel_config_t *c = &n->config[channel];
  switch (property) {
  case MIO_PROP_POLARITY:
    return c->active_low;
  case MIO_PROP_FILTER:
    return c->filter;
  case MIO_PROP_FILTER_MS:
    return c->filter_ms;
  case MIO_PROP_PULL:
    return c->pull;
  case MIO_PROP_MODE:
    return c->mode;
  case MIO_PROP_FREQUENCY:
    return c->frequency_hz;
  case MIO_PROP_DUTY:
    return c->duty_percent_x100;
  case MIO_PROP_PERIOD:
    return c->period_deciseconds;
  case MIO_PROP_VALUE:
    return c->value;
  case MIO_PROP_PULSE_MS:
    return c->pulse_ms;
  case MIO_PROP_TRIGGER:
    return 0;
  case MIO_PROP_PIN:
    return c->pin;
  default:
    return 0;
  }
}

static mio_error_t change(mio_io_node_t *n, const mio_io_request_t *r,
                          uint32_t now, uint16_t *value) {
  uint8_t property = r->property & ~MIO_READ_FLAG;
  uint8_t role = n->board_type == MIO_BOARD_DI4 ? 1 : 2;
  if (!property || property >= MIO_PROP_COUNT ||
      !(mio_properties[property].roles & role)) {
    return MIO_ERR_PROPERTY;
  }
  bool global = property == MIO_PROP_REPORT || property == MIO_PROP_REPORT_MS;
  if ((global && r->channel != MIO_GLOBAL_CHANNEL) ||
      (!global && r->channel >= MIO_CHANNELS)) {
    return MIO_ERR_CHANNEL;
  }
  if (r->property & MIO_READ_FLAG) {
    *value = read_property(n, r->channel, property);
    return MIO_OK;
  }
  const mio_property_info_t *info = &mio_properties[property];
  if (info->readonly) {
    return MIO_ERR_PROPERTY;
  }
  if (r->value < info->minimum || r->value > info->maximum) {
    return MIO_ERR_RANGE;
  }
  if (property == MIO_PROP_REPORT) {
    n->report = r->value;
  } else if (property == MIO_PROP_REPORT_MS) {
    n->report_ms = r->value;
  } else {
    mio_channel_config_t next = n->config[r->channel];
    if (role == 2 && n->fault &&
        !(property == MIO_PROP_VALUE && r->value == 0)) {
      return MIO_ERR_HARDWARE;
    }
    if (property == MIO_PROP_TRIGGER && next.mode != MIO_OUTPUT_PULSE) {
      return MIO_ERR_PROPERTY;
    }
    if (property == MIO_PROP_VALUE && r->value &&
        next.mode == MIO_OUTPUT_PULSE) {
      return MIO_ERR_PROPERTY;
    }
    switch (property) {
    case MIO_PROP_POLARITY:
      next.active_low = r->value;
      break;
    case MIO_PROP_FILTER:
      next.filter = r->value;
      break;
    case MIO_PROP_FILTER_MS:
      next.filter_ms = r->value;
      break;
    case MIO_PROP_PULL:
      next.pull = r->value;
      break;
    case MIO_PROP_MODE:
      next.mode = r->value;
      break;
    case MIO_PROP_FREQUENCY:
      next.frequency_hz = r->value;
      break;
    case MIO_PROP_DUTY:
      next.duty_percent_x100 = r->value;
      break;
    case MIO_PROP_PERIOD:
      next.period_deciseconds = r->value;
      break;
    case MIO_PROP_VALUE:
      next.value = r->value;
      break;
    case MIO_PROP_PULSE_MS:
      next.pulse_ms = r->value;
      break;
    case MIO_PROP_TRIGGER:
      next.value = true;
      break;
    default:
      return MIO_ERR_PROPERTY;
    }
    mio_channel_state_t next_state = n->state[r->channel];
    bool mode_changed = next.mode != n->config[r->channel].mode;
    bool enabling = next.value && !n->config[r->channel].value;
    if (property == MIO_PROP_TRIGGER ||
        (next.mode == MIO_OUTPUT_PULSE && next.value &&
         (mode_changed || property == MIO_PROP_PULSE_MS))) {
      next_state.pulse_until_ms = now + next.pulse_ms;
    }
    /* A period/mode change or explicit enable begins a fresh slow cycle.
     * A duty change preserves the current phase and takes effect at once. */
    if (mode_changed || enabling || property == MIO_PROP_PERIOD) {
      next_state.cycle_since_ms = now;
    }
    bool active = next.value;
    if (active && next.mode == MIO_OUTPUT_SLOW_PWM) {
      uint32_t period_ms = (uint32_t)next.period_deciseconds * 100;
      uint32_t phase = (now - next_state.cycle_since_ms) % period_ms;
      active = phase < ((uint64_t)period_ms * next.duty_percent_x100) / 10000;
    }
    /* Live edits preserve the enable gate. Hardware errors still stop all
     * outputs; each channel uses its own enable gate. */
    if (n->apply && !n->apply(n->context, r->channel, &next, active)) {
      n->fault = true;
      mio_io_node_stop(n);
      return MIO_ERR_HARDWARE;
    }
    next_state.output_active = active;
    if (role == 1) {
      next_state.filtered = false;
      next_state.candidate_since_ms = now;
    }
    n->config[r->channel] = next;
    n->state[r->channel] = next_state;
  }
  *value = property == MIO_PROP_TRIGGER
               ? r->value
               : read_property(n, r->channel, property);
  return MIO_OK;
}

mio_io_reply_t mio_io_node_request(mio_io_node_t *n, const mio_io_request_t *r,
                                   uint32_t now) {
  mio_io_reply_t reply = {.property = r->property,
                          .transaction = r->transaction,
                          .session = r->session};
  if (!r->session || r->session != n->session) {
    reply.error = MIO_ERR_SESSION;
    return reply;
  }
  if (!n->master_alive || (uint32_t)(now - n->heartbeat_ms) >= MIO_LEASE_MS) {
    n->master_alive = false;
    mio_io_node_stop(n);
    reply.error = MIO_ERR_OFFLINE;
    return reply;
  }
  if (n->cached && r->transaction == n->last_request.transaction) {
    /* Replay the reply without executing the operation again. A different
     * request reusing that transaction is a protocol error. */
    if (r->property == n->last_request.property &&
        r->channel == n->last_request.channel &&
        r->value == n->last_request.value) {
      return n->last_reply;
    }
    reply.error = MIO_ERR_SEQUENCE;
    return reply;
  }
  if (!r->transaction ||
      (n->cached && r->transaction < n->last_request.transaction)) {
    reply.error = MIO_ERR_SEQUENCE;
    return reply;
  }
  reply.error = change(n, r, now, &reply.value);
  n->last_request = *r;
  n->last_reply = reply;
  n->cached = true;
  return reply;
}
