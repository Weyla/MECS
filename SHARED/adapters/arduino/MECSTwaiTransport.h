#pragma once

#include "mecs_protocol.h"
#include <stdint.h>

/* ESP32 Arduino adapter for chips with an on-chip TWAI controller.
 * The external CAN transceiver is still required. */
class MECSTwaiTransport {
public:
  bool begin(int tx_gpio, int rx_gpio, uint32_t bitrate = 500000);
  bool send(const mecs_frame_t *frame);
  bool receive(mecs_frame_t *frame);
  bool poll(); /* True once after bus-off recovery; called from loop(). */
  void end();

private:
  bool started_ = false;
  bool recovering_ = false;
};
