#pragma once

/* Beginner-friendly ESP-IDF binding for the portable node/pin API. */
#include "MECS.h"
#include "esp_err.h"
#include <stdint.h>

class MECSClient : public MECSController {
public:
  /* Initializes NVS, persists a fresh session, and starts CAN/TWAI. */
  esp_err_t begin(int txGpio, int rxGpio, uint32_t bitrate = 500000);

  /* Call often from the same task that uses pin and node objects. */
  void loop();

  /* Optional readable node and request messages through ESP-IDF logging. */
  void enableLogging();
  void disableLogging();

private:
  using MECSController::start;
  using MECSController::receive;
  using MECSController::service;
  using MECSController::transportRecovered;

  bool started_ = false;
  bool attemptedSessionRenewal_ = false;
  uint32_t lastSessionRenewalMs_ = 0;

  static void logEvent(void *context, mecs_client_event_t event,
                       uint8_t node, uint8_t channel, uint8_t property,
                       uint32_t value, mecs_error_t error);
};
