#pragma once

#include "MIOArduinoTwai.h"
#include "mio_client.h"

/* Small C++ wrapper for Arduino sketches. The portable C API remains
 * available for applications that need lower-level typed properties. */
class MIOClient {
public:
  bool begin(int tx_gpio, int rx_gpio,
             mio_client_event_fn on_event = nullptr,
             void *event_context = nullptr, uint32_t bitrate = 500000);
  void loop();
  bool discover();
  bool set(uint8_t node, uint8_t channel, mio_property_t property,
           uint16_t value);
  bool get(uint8_t node, uint8_t channel, mio_property_t property);
  bool setOutput(uint8_t node, uint8_t channel, bool enabled);
  bool setPwmDuty(uint8_t node, uint8_t channel, float percent);
  bool setPwmFrequency(uint8_t node, uint8_t channel, uint16_t hertz);
  const mio_client_node_t *node(uint8_t node_id) const;
  bool nodeOnline(uint8_t node_id) const;
  bool requestPending() const;
  uint16_t session() const { return session_; }

private:
  static bool sendFrame(void *context, const mio_frame_t *frame);
  MIOArduinoTwai transport_;
  mio_client_t client_{};
  bool ready_ = false;
  uint16_t session_ = 0;
};
