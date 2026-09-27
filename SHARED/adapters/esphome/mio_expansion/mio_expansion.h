#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "mio_client.h"

#include <vector>

namespace esphome::mio_expansion {

class MIOExpansion;

class MIOOutputSwitch : public switch_::Switch {
 public:
  void set_parent(MIOExpansion *parent) { parent_ = parent; }
  void set_node(uint8_t node) { node_ = node; }
  void set_channel(uint8_t channel) { channel_ = channel; }
  uint8_t node() const { return node_; }
  uint8_t channel() const { return channel_; }

 protected:
  void write_state(bool state) override;
  MIOExpansion *parent_{nullptr};
  uint8_t node_{0};
  uint8_t channel_{0};
};

class MIOExpansion : public Component {
 public:
  void set_canbus(canbus::Canbus *bus) { canbus_ = bus; }
  void add_input(uint8_t node, uint8_t channel, binary_sensor::BinarySensor *sensor);
  void add_output(MIOOutputSwitch *output) { outputs_.push_back(output); }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::AFTER_HARDWARE; }

  bool set_output(uint8_t node, uint8_t channel, bool enabled);

 protected:
  struct InputBinding {
    uint8_t node;
    uint8_t channel;
    binary_sensor::BinarySensor *sensor;
  };
  static bool send_frame(void *context, const mio_frame_t *frame);
  static void on_event(void *context, mio_client_event_t event,
                       uint8_t node, uint8_t channel, uint8_t property,
                       uint16_t value, mio_error_t error);
  void on_frame(uint32_t id, bool extended, bool remote,
                const std::vector<uint8_t> &data);

  canbus::Canbus *canbus_{nullptr};
  uint16_t session_{0};
  mio_client_t client_{};
  std::vector<InputBinding> inputs_;
  std::vector<MIOOutputSwitch *> outputs_;
};

}  // namespace esphome::mio_expansion
