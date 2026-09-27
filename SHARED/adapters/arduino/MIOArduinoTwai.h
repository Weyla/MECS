#pragma once

#include "mio.h"
#include <stdint.h>

/* ESP32 Arduino adapter for chips with an on-chip TWAI controller.
 * The external CAN transceiver is still required. */
class MIOArduinoTwai {
public:
  bool begin(int tx_gpio, int rx_gpio, uint32_t bitrate = 500000);
  bool send(const mio_frame_t *frame);
  bool receive(mio_frame_t *frame);
  void end();

private:
  bool started_ = false;
};
