#pragma once
#include <Arduino.h>
#include "MECS.h"
#include "MECSTwaiTransport.h"

/* Arduino owns only time, logging, persistent sessions and the CAN driver.
 * Discovery, configuration/recovery and pin objects are portable MECS code. */
class MECSClient : public MECSController {
public:
  bool begin(int txGpio, int rxGpio, uint32_t bitrate = 500000);
  void loop();
  void enableLogging(Print &output); // Optional; no user callback is required.
  void disableLogging();
private:
  MECSTwaiTransport transport_;
  bool started_ = false;
  static bool send(void *, const mecs_frame_t *);
  static void log(void *, mecs_client_event_t, uint8_t, uint8_t, uint8_t,
                  uint32_t, mecs_error_t);
};
