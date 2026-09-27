#include "MECSClient.h"
#include <Preferences.h>

namespace {
uint16_t nextSession() {
  Preferences storage;
  if (!storage.begin("mecs", false)) return 0;
  uint16_t previous = storage.getUShort("session", 0);
  if (!storage.isKey("session")) {
    // Read the earlier MIO namespace once so powered nodes see a new session.
    Preferences previousStorage;
    if (previousStorage.begin("mio", true)) {
      previous = previousStorage.getUShort("session", 0);
      previousStorage.end();
    }
  }
  const uint16_t next = previous == UINT16_MAX ? 1 : previous + 1;
  const bool saved = storage.putUShort("session", next) == sizeof(next);
  storage.end();
  return saved ? next : 0;
}
}
bool MECSClient::send(void *context, const mecs_frame_t *frame) {
  return static_cast<MECSTwaiTransport *>(context)->send(frame);
}
bool MECSClient::begin(int txGpio, int rxGpio, uint32_t bitrate) {
  if (started_ || !transport_.begin(txGpio, rxGpio, bitrate)) return false;
  const uint16_t session = nextSession();
  mecs_client_config_t config{};
  config.send_frame = send;
  config.transport_context = &transport_;
  config.session = session;
  if (!session || !start(config)) {
    transport_.end();
    return false;
  }
  started_ = true;
  return true;
}
void MECSClient::loop() {
  if (!started_) return;
  const uint32_t now = millis();
  if (transport_.poll()) transportRecovered();
  mecs_frame_t frame;
  for (unsigned count = 0; count < 24 && transport_.receive(&frame); ++count)
    receive(frame, now);
  if (needsNewSession()) {
    const uint16_t session = nextSession();
    if (session) setSession(session);
  }
  service(now);
}
void MECSClient::enableLogging(Print &output) { setLogger(log, &output); }
void MECSClient::disableLogging() { setLogger(nullptr, nullptr); }
void MECSClient::log(void *context, mecs_client_event_t event, uint8_t node,
                      uint8_t channel, uint8_t property, uint32_t value,
                      mecs_error_t error) {
  auto &out = *static_cast<Print *>(context);
  if (event == MECS_CLIENT_EVENT_NODE_ANNOUNCED) {
    out.print("MECS node "); out.print(node); out.print(" announced; board ");
    out.println(value);
  } else if (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED ||
             event == MECS_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MECS_CLIENT_EVENT_REQUEST_TIMEOUT) {
    out.print("MECS node "); out.print(node);
    out.print(" channel "); out.print(channel);
    out.print(' '); out.print(mecs_properties[property].name);
    if (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED) {
      out.print(" confirmed: ");
      const auto scale = mecs_properties[property].scale;
      if (scale == 1000) out.print(value / 1000.0, 3);
      else if (scale == 100) out.print(value / 100.0, 2);
      else if (scale == 10) out.print(value / 10.0, 1);
      else out.print(value);
      out.print(' '); out.println(mecs_properties[property].unit);
    } else {
      out.print(" failed: "); out.println(mecs_error_name(error));
    }
  }
}
