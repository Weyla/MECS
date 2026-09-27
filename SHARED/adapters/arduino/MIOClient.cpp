#include "MIOClient.h"

#include <Arduino.h>
#include <Preferences.h>

static uint16_t next_persistent_session() {
  Preferences preferences;
  if (!preferences.begin("mio", false)) {
    return 0;
  }
  uint16_t next = preferences.getUShort("session", 0);
  next = next == UINT16_MAX ? 1u : next + 1u;
  const bool saved = preferences.putUShort("session", next) == sizeof(next);
  preferences.end();
  return saved ? next : 0;
}

bool MIOClient::sendFrame(void *context, const mio_frame_t *frame) {
  return static_cast<MIOArduinoTwai *>(context)->send(frame);
}

bool MIOClient::begin(int tx_gpio, int rx_gpio,
                      mio_client_event_fn on_event, void *event_context,
                      uint32_t bitrate) {
  if (!transport_.begin(tx_gpio, rx_gpio, bitrate)) {
    return false;
  }
  session_ = next_persistent_session();
  if (!session_) {
    transport_.end();
    return false;
  }
  mio_client_config_t config = {};
  config.send_frame = sendFrame;
  config.transport_context = &transport_;
  config.session = session_;
  ready_ = mio_client_begin(&client_, &config, on_event, event_context);
  return ready_;
}

void MIOClient::loop() {
  if (!ready_) {
    return;
  }
  const uint32_t now = millis();
  mio_frame_t frame;
  for (unsigned count = 0; count < 24 && transport_.receive(&frame); ++count) {
    mio_client_receive(&client_, &frame, now);
  }
  if (mio_client_needs_new_session(&client_)) {
    const uint16_t next = next_persistent_session();
    if (next && mio_client_set_session(&client_, next)) {
      session_ = next;
    }
  }
  mio_client_loop(&client_, now);
}

bool MIOClient::discover() {
  return ready_ && mio_client_discover(&client_);
}

bool MIOClient::set(uint8_t node, uint8_t channel, mio_property_t property,
                    uint16_t value) {
  return ready_ && mio_client_set(&client_, node, channel, property, value);
}

bool MIOClient::get(uint8_t node, uint8_t channel, mio_property_t property) {
  return ready_ && mio_client_get(&client_, node, channel, property);
}

bool MIOClient::setOutput(uint8_t node, uint8_t channel, bool enabled) {
  return set(node, channel, MIO_PROP_VALUE, enabled ? 1 : 0);
}

bool MIOClient::setPwmDuty(uint8_t node, uint8_t channel, float percent) {
  if (percent < 0.0f || percent > 100.0f) {
    return false;
  }
  const uint16_t hundredths = static_cast<uint16_t>(percent * 100.0f + 0.5f);
  return set(node, channel, MIO_PROP_DUTY, hundredths);
}

bool MIOClient::setPwmFrequency(uint8_t node, uint8_t channel,
                                uint16_t hertz) {
  return set(node, channel, MIO_PROP_FREQUENCY, hertz);
}

const mio_client_node_t *MIOClient::node(uint8_t node_id) const {
  return ready_ ? mio_client_node(&client_, node_id) : nullptr;
}

bool MIOClient::nodeOnline(uint8_t node_id) const {
  return ready_ && mio_client_node_online(mio_client_node(&client_, node_id),
                                          millis());
}

bool MIOClient::requestPending() const {
  return ready_ && mio_client_request_pending(&client_);
}
