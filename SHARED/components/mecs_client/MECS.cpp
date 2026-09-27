#include "MECS.h"
#include <cmath>

namespace {
unsigned slot(uint8_t channel) {
  return channel == MECS_GLOBAL_CHANNEL ? MECS_CHANNELS : channel;
}
bool percent(double value, uint32_t &scaled) {
  if (!std::isfinite(value) || value < 0.0 || value > 100.0) return false;
  scaled = static_cast<uint32_t>(value * 1000.0 + 0.5);
  return true;
}
// Mode and all its parameters precede enable, regardless of setter call order.
const mecs_property_t order[] = {
    MECS_PROP_MODE, MECS_PROP_FILTER, MECS_PROP_POLARITY, MECS_PROP_PULL,
    MECS_PROP_FILTER_MS, MECS_PROP_FREQUENCY, MECS_PROP_PERIOD,
    MECS_PROP_DUTY_PRECISE, MECS_PROP_REPORT_MS, MECS_PROP_REPORT_MIN_MS,
    MECS_PROP_REPORT, MECS_PROP_VALUE};
}

MECSController::Node *MECSController::find(uint8_t address) {
  for (auto &n : nodes_) if (n.address == address && address) return &n;
  return nullptr;
}
const MECSController::Node *MECSController::find(uint8_t address) const {
  for (const auto &n : nodes_) if (n.address == address && address) return &n;
  return nullptr;
}
MECSNode MECSController::node(uint8_t address) {
  if (address && address <= MECS_MAX_NODE_ID && !find(address)) {
    for (auto &n : nodes_) {
      if (!n.address) { n.address = address; break; }
    }
  }
  return MECSNode(this, address);
}
bool MECSController::start(const mecs_client_config_t &config) {
  if (started_) return false;
  started_ = mecs_client_begin(&client_, &config, event, this);
  return started_;
}
void MECSController::receive(const mecs_frame_t &frame, uint32_t nowMs) {
  now_ = nowMs;
  if (started_) mecs_client_receive(&client_, &frame, nowMs);
}
void MECSController::service(uint32_t nowMs) {
  now_ = nowMs;
  if (!started_) return;
  synchronize();
  mecs_client_loop(&client_, nowMs);
  schedule();
}
void MECSController::transportRecovered() {
  if (started_) mecs_client_transport_reset(&client_);
}
bool MECSController::needsNewSession() const {
  return started_ && mecs_client_needs_new_session(&client_);
}
bool MECSController::setSession(uint16_t session) {
  return started_ && mecs_client_set_session(&client_, session);
}
void MECSController::setLogger(mecs_client_event_fn logger, void *context) {
  logger_ = logger; loggerContext_ = context;
}

bool MECSController::invalid(uint8_t address, uint8_t channel, mecs_error_t e) {
  auto *n = find(address);
  if (n && slot(channel) <= MECS_CHANNELS) n->channels[slot(channel)].error = e;
  return false;
}
bool MECSController::want(uint8_t address, uint8_t channel,
                           mecs_property_t property, uint32_t value) {
  auto *n = find(address);
  if (!n || (channel >= MECS_CHANNELS && channel != MECS_GLOBAL_CHANNEL)) return false;
  const bool global = property == MECS_PROP_REPORT || property == MECS_PROP_REPORT_MS ||
                      property == MECS_PROP_REPORT_MIN_MS;
  if (global != (channel == MECS_GLOBAL_CHANNEL)) return false;
  auto &c = n->channels[slot(channel)];
  const auto &info = mecs_properties[property];
  if (value < info.minimum || value > info.maximum)
    return invalid(address, channel, MECS_ERR_RANGE);
  if (info.roles != 3 && c.role && c.role != info.roles)
    return invalid(address, channel, MECS_ERR_PROPERTY);
  if (info.roles != 3) {
    if (!c.role && info.roles == 2) c.needsOff = true;
    c.role = info.roles;
  }
  if (!c.failed) c.error = MECS_OK;
  const uint32_t bit = 1u << property;
  if (!(c.wanted & bit) || c.desired[property] != value) {
    c.desired[property] = value;
    c.wanted |= bit;
    c.confirmed &= ~bit;
    c.failed &= ~bit;
    if (!c.failed) c.error = MECS_OK;
    c.retryAfter = now_;
  }
  return true; // Desired state recorded, not yet necessarily applied.
}

/* Reboots, lease loss, transport recovery and master-session rollover all
 * invalidate confirmations. Requested settings remain in RAM and are replayed
 * only after fresh status. No old queue or success flag can bypass this step. */
void MECSController::synchronize() {
  for (auto &n : nodes_) {
    if (!n.address) continue;
    const auto *seen = mecs_client_node(&client_, n.address);
    if (!seen) continue;
    if (seen->generation != n.generation) {
      n.generation = seen->generation;
      for (auto &c : n.channels) {
        c.confirmed = c.failed = 0;
        if (!c.failed) c.error = MECS_OK;
        c.retryAfter = now_;
        c.needsOff = c.role == 2;
        c.needsInputStatus = false;
      }
    }
    for (auto &c : n.channels)
      if (c.needsInputStatus && seen->status.sequence != c.inputStatusSequence)
        c.needsInputStatus = false;
  }
}

/* At most ONE setting is awaiting confirmation. This bounds bus work, avoids
 * partially enqueued setup chains and lets repeated loop() setters coalesce
 * into the newest requested value. Round-robin channels prevent starvation. */
void MECSController::schedule() {
  if (pending_.active) return;
  constexpr unsigned slots = MECS_CLIENT_MAX_NODES * (MECS_CHANNELS + 1);
  for (unsigned visited = 0; visited < slots; ++visited) {
    const unsigned index = cursor_;
    cursor_ = (cursor_ + 1) % slots;
    auto &n = nodes_[index / (MECS_CHANNELS + 1)];
    auto &c = n.channels[index % (MECS_CHANNELS + 1)];
    if (!n.address || !c.wanted || !online(n.address) ||
        static_cast<int32_t>(now_ - c.retryAfter) < 0) continue;
    const auto *seen = mecs_client_node(&client_, n.address);
    const uint32_t dirty = c.wanted & ~c.confirmed;
    const bool explicitOff = (dirty & (1u << MECS_PROP_VALUE)) &&
                             !c.desired[MECS_PROP_VALUE];
    if (!seen->status.master_alive ||
        (seen->status.fault && !explicitOff && !c.needsOff)) continue;
    const unsigned role = seen->identity.board_type == MECS_BOARD_DI4 ? 1 :
                          seen->identity.board_type == MECS_BOARD_DO4 ? 2 : 0;
    if (!role || (c.role && c.role != role)) {
      c.error = MECS_ERR_PROPERTY;
      continue;
    }
    const uint8_t channel = index % (MECS_CHANNELS + 1) == MECS_CHANNELS
                                ? MECS_GLOBAL_CHANNEL : index % (MECS_CHANNELS + 1);
    // Never enable a channel while ANY of its settings failed or is pending.
    // An explicit off remains usable even if another property was rejected.
    if (c.failed && !explicitOff && !c.needsOff) continue;
    mecs_property_t property = MECS_PROP_VALUE;
    uint32_t value = 0;
    const bool initialOff = c.needsOff;
    if (!initialOff) {
      if (!dirty) continue;
      // Explicit off takes precedence over parameter edits.
      if ((dirty & (1u << MECS_PROP_VALUE)) && !c.desired[MECS_PROP_VALUE]) {
        property = MECS_PROP_VALUE;
      } else {
        for (auto p : order) {
          if (dirty & (1u << p)) { property = p; break; }
        }
      }
      value = c.desired[property];
    }
    if (mecs_client_set(&client_, n.address, channel, property, value)) {
      pending_.active = true;
      pending_.initialOff = initialOff;
      pending_.node = n.address;
      pending_.channel = channel;
      pending_.property = property;
      pending_.value = value;
      pending_.generation = n.generation;
      return;
    }
    c.error = MECS_ERR_OFFLINE;
    c.retryAfter = now_ + 250;
  }
}

void MECSController::event(void *context, mecs_client_event_t event,
                            uint8_t address, uint8_t channel, uint8_t property,
                            uint32_t value, mecs_error_t error) {
  auto &self = *static_cast<MECSController *>(context);
  auto &pending = self.pending_;
  if (pending.active && pending.node == address && pending.channel == channel &&
      pending.property == property &&
      (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED ||
       event == MECS_CLIENT_EVENT_REQUEST_REJECTED ||
       event == MECS_CLIENT_EVENT_REQUEST_TIMEOUT)) {
    auto *n = self.find(address);
    auto &c = n->channels[slot(channel)];
    const auto *seen = mecs_client_node(&self.client_, address);
    if (seen && seen->generation == pending.generation) {
      if (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED && value == pending.value) {
        if (pending.initialOff) c.needsOff = false;
        if ((c.wanted & (1u << property)) && c.desired[property] == value)
          c.confirmed |= 1u << property;
        if (c.role == 1) {
          c.needsInputStatus = true;
          c.inputStatusSequence = seen->status.sequence;
        }
        // A successful update to one setting must not hide an earlier
        // rejection of a different setting on this channel.
        if (!c.failed) c.error = MECS_OK;
      } else {
        c.error = error == MECS_OK ? MECS_ERR_SEQUENCE : error;
        c.retryAfter = self.now_ + 250;
        if (c.error == MECS_ERR_PROPERTY || c.error == MECS_ERR_RANGE ||
            c.error == MECS_ERR_CHANNEL || c.error == MECS_ERR_HARDWARE)
          c.failed |= 1u << property;
      }
    }
    pending.active = false;
  }
  if (self.logger_) self.logger_(self.loggerContext_, event, address, channel,
                                 property, value, error);
}

bool MECSController::online(uint8_t address) const {
  return started_ && mecs_client_node_online(mecs_client_node(&client_, address), now_);
}
bool MECSController::ready(uint8_t address, uint8_t channel) const {
  const auto *n = find(address);
  if (!n || (channel >= MECS_CHANNELS && channel != MECS_GLOBAL_CHANNEL) || !online(address)) return false;
  const auto *seen = mecs_client_node(&client_, address);
  if (!seen->status.master_alive || seen->status.fault ||
      seen->generation != n->generation) return false;
  const auto &c = n->channels[slot(channel)];
  return !c.needsOff && !c.failed && c.error == MECS_OK &&
         (c.wanted & c.confirmed) == c.wanted;
}
mecs_error_t MECSController::error(uint8_t address, uint8_t channel) const {
  if (channel >= MECS_CHANNELS && channel != MECS_GLOBAL_CHANNEL) return MECS_ERR_CHANNEL;
  const auto *n = find(address);
  if (!n) return MECS_ERR_CHANNEL;
  if (n->channels[slot(channel)].error != MECS_OK)
    return n->channels[slot(channel)].error;
  if (!online(address)) return MECS_ERR_OFFLINE;
  return mecs_client_node(&client_, address)->status.fault ? MECS_ERR_HARDWARE : MECS_OK;
}
bool MECSController::inputValid(uint8_t address, uint8_t channel) const {
  if (!(channel < MECS_CHANNELS && ready(address, channel) &&
         mecs_client_node(&client_, address)->identity.board_type == MECS_BOARD_DI4 &&
         ready(address, MECS_GLOBAL_CHANNEL))) return false;
  const auto &c = find(address)->channels[channel];
  return !c.needsInputStatus ||
         mecs_client_node(&client_, address)->status.sequence != c.inputStatusSequence;
}

bool MECSPin::setMode(MECS::Mode mode) {
  if (channel_ >= MECS_CHANNELS) return false;
  bool accepted;
  switch (mode) {
  case MECS::DIGITAL_OUTPUT: case MECS::PWM: case MECS::SLOW_PWM:
    accepted = client_->want(node_, channel_, MECS_PROP_MODE,
        mode == MECS::PWM ? MECS_OUTPUT_PWM :
        mode == MECS::SLOW_PWM ? MECS_OUTPUT_SLOW_PWM : MECS_OUTPUT_DIGITAL);
    break;
  case MECS::DIGITAL_INPUT: case MECS::DEBOUNCE: case MECS::PWM_INPUT:
    accepted = client_->want(node_, channel_, MECS_PROP_FILTER,
        mode == MECS::DEBOUNCE ? MECS_FILTER_STABLE :
        mode == MECS::PWM_INPUT ? MECS_FILTER_PWM : MECS_FILTER_NONE);
    break;
  default: return client_->invalid(node_, channel_, MECS_ERR_RANGE);
  }
  if (!accepted) return false;
  // An unspecified polarity means active-high. Explicit polarity is preserved.
  auto *n = client_->find(node_);
  auto &c = n->channels[channel_];
  if (!(c.wanted & (1u << MECS_PROP_POLARITY))) setActiveLow(false);
  return true;
}
bool MECSPin::setPwm(uint16_t frequencyHz, double dutyPercent) {
  uint32_t duty;
  if (frequencyHz < 10 || frequencyHz > 10000 || !percent(dutyPercent, duty))
    return client_->invalid(node_, channel_, MECS_ERR_RANGE);
  return setMode(MECS::PWM) &&
         client_->want(node_, channel_, MECS_PROP_FREQUENCY, frequencyHz) &&
         client_->want(node_, channel_, MECS_PROP_DUTY_PRECISE, duty);
}
bool MECSPin::setDuty(double dutyPercent) {
  uint32_t duty;
  if (!percent(dutyPercent, duty))
    return client_->invalid(node_, channel_, MECS_ERR_RANGE);
  return client_->want(node_, channel_, MECS_PROP_DUTY_PRECISE, duty);
}
bool MECSPin::setSlowPwm(double seconds, double dutyPercent) {
  uint32_t duty;
  if (!std::isfinite(seconds) || seconds < 0.1 || seconds > 3600 ||
      !percent(dutyPercent, duty))
    return client_->invalid(node_, channel_, MECS_ERR_RANGE);
  return setMode(MECS::SLOW_PWM) &&
         client_->want(node_, channel_, MECS_PROP_PERIOD,
                        static_cast<uint32_t>(seconds * 10.0 + 0.5)) &&
         client_->want(node_, channel_, MECS_PROP_DUTY_PRECISE, duty);
}
bool MECSPin::setActiveLow(bool activeLow) {
  return client_->want(node_, channel_, MECS_PROP_POLARITY, activeLow);
}
bool MECSPin::setDebounce(uint16_t milliseconds) {
  if (!milliseconds || milliseconds > 1000)
    return client_->invalid(node_, channel_, MECS_ERR_RANGE);
  return setMode(MECS::DEBOUNCE) &&
         client_->want(node_, channel_, MECS_PROP_FILTER_MS, milliseconds);
}
bool MECSPin::setResistor(MECS::Resistor resistor) {
  return client_->want(node_, channel_, MECS_PROP_PULL, static_cast<unsigned>(resistor));
}
bool MECSPin::turnOn() { return client_->want(node_, channel_, MECS_PROP_VALUE, 1); }
bool MECSPin::turnOff() { return client_->want(node_, channel_, MECS_PROP_VALUE, 0); }
bool MECSPin::ready() const {
  return channel_ < MECS_CHANNELS && client_->ready(node_, channel_);
}
bool MECSPin::valid() const { return client_->inputValid(node_, channel_); }
bool MECSPin::value() const {
  return valid() && ((mecs_client_node(&client_->client_, node_)->status.logical >> channel_) & 1u);
}
MECSPwmMeasurement MECSPin::pwm() const {
  MECSPwmMeasurement result;
  if (!valid()) return result;
  const auto &c = client_->find(node_)->channels[channel_];
  if (!(c.wanted & (1u << MECS_PROP_FILTER)) ||
      c.desired[MECS_PROP_FILTER] != MECS_FILTER_PWM) return result;
  const auto *n = mecs_client_node(&client_->client_, node_);
  const auto &measured = n->measurement[channel_];
  if (!measured.valid || !measured.period_us ||
      (uint32_t)(client_->now_ - n->measurement_ms[channel_]) >=
          MECS_CLIENT_NODE_ONLINE_MS) return result;
  result.valid = true;
  result.frequencyHz = 1000000.0 / measured.period_us;
  result.periodSeconds = measured.period_us / 1000000.0;
  result.dutyPercent = measured.duty_percent_x100 / 100.0;
  return result;
}
mecs_error_t MECSPin::lastError() const {
  return channel_ < MECS_CHANNELS ? client_->error(node_, channel_) : MECS_ERR_CHANNEL;
}
bool MECSNode::setReporting(uint16_t minimumMs, uint16_t maximumMs, bool onChange) {
  if (maximumMs < 20 || maximumMs > 500 || minimumMs > maximumMs)
    return client_->invalid(node_, MECS_GLOBAL_CHANNEL, MECS_ERR_RANGE);
  return client_->want(node_, MECS_GLOBAL_CHANNEL, MECS_PROP_REPORT_MS, maximumMs) &&
         client_->want(node_, MECS_GLOBAL_CHANNEL, MECS_PROP_REPORT_MIN_MS, minimumMs) &&
         client_->want(node_, MECS_GLOBAL_CHANNEL, MECS_PROP_REPORT,
                        onChange ? MECS_REPORT_BOTH : MECS_REPORT_PERIODIC);
}
bool MECSNode::online() const { return client_->online(node_); }
bool MECSNode::ready() const {
  for (unsigned i = 0; i <= MECS_CHANNELS; ++i)
    if (!client_->ready(node_, i == MECS_CHANNELS ? MECS_GLOBAL_CHANNEL : i)) return false;
  return true;
}
mecs_error_t MECSNode::lastError() const {
  for (unsigned i = 0; i <= MECS_CHANNELS; ++i) {
    const auto e = client_->error(node_, i == MECS_CHANNELS ? MECS_GLOBAL_CHANNEL : i);
    if (e != MECS_OK) return e;
  }
  return MECS_OK;
}
