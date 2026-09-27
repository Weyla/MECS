#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/canbus/canbus.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
#include "mecs_client.h"

#include <vector>

namespace esphome::mecs_expansion {

class MECSExpansion;

class MECSOutputSwitch : public switch_::Switch {
 public:
  void set_parent(MECSExpansion *parent) { parent_ = parent; }
  void set_node(uint8_t node) { node_ = node; }
  void set_channel(uint8_t channel) { channel_ = channel; }
  uint8_t node() const { return node_; }
  uint8_t channel() const { return channel_; }

 protected:
  void write_state(bool state) override;
  MECSExpansion *parent_{nullptr};
  uint8_t node_{0};
  uint8_t channel_{0};
};

class MECSExpansion : public Component {
 public:
  void set_canbus(canbus::Canbus *bus) { canbus_ = bus; }
  void add_input(uint8_t node, uint8_t channel, binary_sensor::BinarySensor *sensor);
  void add_output(MECSOutputSwitch *output) { outputs_.push_back({output, false}); }
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
  struct OutputBinding {
    MECSOutputSwitch *output;
    bool was_online;
  };
  static bool send_frame(void *context, const mecs_frame_t *frame);
  static void on_event(void *context, mecs_client_event_t event,
                       uint8_t node, uint8_t channel, uint8_t property,
                       uint32_t value, mecs_error_t error);
  void refresh_entity_states(uint32_t now_ms);
  void on_frame(uint32_t id, bool extended, bool remote,
                const std::vector<uint8_t> &data);

  canbus::Canbus *canbus_{nullptr};
  uint16_t session_{0};
  mecs_client_t client_{};
  std::vector<InputBinding> inputs_;
  std::vector<OutputBinding> outputs_;
};

}  // namespace esphome::mecs_expansion
