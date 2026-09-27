#pragma once

/* Portable, allocation-free MECS API. A framework adapter supplies CAN frames
 * and time; node/pin handles only identify entries owned by this controller. */
#include "mecs_client.h"

namespace MECS {
enum Mode { DIGITAL_OUTPUT, PWM, SLOW_PWM, DIGITAL_INPUT, DEBOUNCE, PWM_INPUT };
enum Resistor { Floating, PullUp, PullDown };
}

struct MECSPwmMeasurement {
  bool valid = false;
  double frequencyHz = 0, periodSeconds = 0, dutyPercent = 0;
};

class MECSController;
class MECSPin {
public:
  bool setMode(MECS::Mode mode);
  bool setPwm(uint16_t frequencyHz, double dutyPercent);
  bool setDuty(double dutyPercent);
  bool setSlowPwm(double periodSeconds, double dutyPercent);
  bool setActiveLow(bool activeLow);
  bool setDebounce(uint16_t milliseconds);
  bool setResistor(MECS::Resistor resistor);
  bool turnOn();
  bool turnOff();
  bool ready() const; // All requested settings acknowledged by the current node.
  bool valid() const; // Fresh input status, and its requested settings are ready.
  bool value() const; // Logical level; false when input data is not valid.
  MECSPwmMeasurement pwm() const; // Invalid when missing, stale or not PWM mode.
  mecs_error_t lastError() const;
private:
  friend class MECSNode;
  MECSPin(MECSController *client, uint8_t node, uint8_t channel)
      : client_(client), node_(node), channel_(channel) {}
  MECSController *client_;
  uint8_t node_, channel_;
};

class MECSNode {
public:
  MECSPin pin(uint8_t channel) { return MECSPin(client_, node_, channel); }
  bool setReporting(uint16_t minimumMs, uint16_t maximumMs, bool onChange);
  bool online() const;
  bool ready() const;
  mecs_error_t lastError() const;
private:
  friend class MECSController;
  MECSNode(MECSController *client, uint8_t node) : client_(client), node_(node) {}
  MECSController *client_;
  uint8_t node_;
};

class MECSController {
public:
  MECSNode node(uint8_t address);
  /* Framework boundary. start() preserves settings registered beforehand.
   * service()/receive() must run in one task, never in an interrupt. */
  bool start(const mecs_client_config_t &config);
  void receive(const mecs_frame_t &frame, uint32_t nowMs);
  void service(uint32_t nowMs);
  void transportRecovered();
  bool needsNewSession() const;
  bool setSession(uint16_t session);
  void setLogger(mecs_client_event_fn logger, void *context);
  MECSController() = default;
  MECSController(const MECSController &) = delete;
  MECSController &operator=(const MECSController &) = delete;

private:
  friend class MECSPin;
  friend class MECSNode;
  struct Channel {
    uint32_t desired[MECS_PROP_COUNT]{};
    uint32_t wanted = 0, confirmed = 0, failed = 0;
    uint32_t retryAfter = 0;
    uint8_t role = 0; // 1=input, 2=output, 0=not yet specified.
    bool needsOff = false;
    bool needsInputStatus = false;
    uint16_t inputStatusSequence = 0;
    mecs_error_t error = MECS_OK;
  };
  struct Node {
    uint8_t address = 0;
    uint32_t generation = 0;
    Channel channels[MECS_CHANNELS + 1]{}; // Last entry is node-wide settings.
  };
  struct Pending {
    bool active = false, initialOff = false;
    uint8_t node = 0, channel = 0, property = 0;
    uint32_t value = 0, generation = 0;
  } pending_;
  Node nodes_[MECS_CLIENT_MAX_NODES]{};
  mecs_client_t client_{};
  bool started_ = false;
  uint32_t now_ = 0;
  unsigned cursor_ = 0;
  mecs_client_event_fn logger_ = nullptr;
  void *loggerContext_ = nullptr;

  Node *find(uint8_t address);
  const Node *find(uint8_t address) const;
  bool want(uint8_t node, uint8_t channel, mecs_property_t property, uint32_t value);
  bool ready(uint8_t node, uint8_t channel) const;
  bool online(uint8_t node) const;
  bool inputValid(uint8_t node, uint8_t channel) const;
  mecs_error_t error(uint8_t node, uint8_t channel) const;
  bool invalid(uint8_t node, uint8_t channel, mecs_error_t error);
  static void event(void *, mecs_client_event_t, uint8_t, uint8_t, uint8_t,
                    uint32_t, mecs_error_t);
  void synchronize();
  void schedule();
};
