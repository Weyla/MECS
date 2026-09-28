#include "mecs_expansion.h"

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::mecs_expansion {

static const char *const TAG = "mecs_expansion";
static constexpr uint32_t SESSION_PREFERENCE_KEY = 0x4D454353u;
// Keep this old MIO key only to migrate stored sessions during an upgrade.
static constexpr uint32_t PREVIOUS_SESSION_PREFERENCE_KEY = 0x4D494F31u;

void MECSExpansion::add_input(uint8_t node, uint8_t channel,
                             binary_sensor::BinarySensor *sensor) {
  inputs_.push_back({node, channel, sensor});
}

bool MECSExpansion::send_frame(void *context, const mecs_frame_t *frame) {
  auto *self = static_cast<MECSExpansion *>(context);
  std::vector<uint8_t> data(frame->data, frame->data + frame->length);
  return self->canbus_->send_data(frame->id, frame->extended, frame->remote,
                                  data) == canbus::ERROR_OK;
}

bool MECSExpansion::save_next_session() {
  /* A session is usable only after the preference reaches persistent storage. */
  auto preference = global_preferences->make_preference<uint16_t>(
      SESSION_PREFERENCE_KEY, true);
  uint16_t previous_session = 0;
  if (!preference.load(&previous_session)) {
    auto previous_preference = global_preferences->make_preference<uint16_t>(
        PREVIOUS_SESSION_PREFERENCE_KEY, true);
    (void)previous_preference.load(&previous_session);
  }
  const uint16_t next = previous_session == UINT16_MAX ? 1u : previous_session + 1u;
  if (!preference.save(&next) || !global_preferences->sync()) {
    ESP_LOGE(TAG, "Could not save master session");
    return false;
  }
  session_ = next;
  return true;
}

void MECSExpansion::setup() {
  if (canbus_ == nullptr) {
    ESP_LOGE(TAG, "A CAN bus is required");
    mark_failed();
    return;
  }
  if (!save_next_session()) {
    mark_failed();
    return;
  }
  const mecs_client_config_t config = {send_frame, this, session_};
  if (!mecs_client_begin(&client_, &config, on_event, this)) {
    ESP_LOGE(TAG, "MECS client initialization failed");
    mark_failed();
    return;
  }
  canbus_->add_callback([this](uint32_t id, bool extended, bool remote,
                               const std::vector<uint8_t> &data) {
    this->on_frame(id, extended, remote, data);
  });
  ESP_LOGI(TAG, "MECS expansion client ready");
}

void MECSExpansion::loop() {
  if (is_failed()) return;
  const uint32_t now_ms = millis();
  if (mecs_client_needs_new_session(&client_) &&
      (!session_retry_pending_ || (uint32_t)(now_ms - session_retry_ms_) >= 1000)) {
    session_retry_pending_ = true;
    session_retry_ms_ = now_ms;
    if (save_next_session() && mecs_client_set_session(&client_, session_)) {
      session_retry_pending_ = false;
      ESP_LOGI(TAG, "Master session renewed: %u; outputs require explicit enabling", session_);
    }
  }
  mecs_client_loop(&client_, now_ms);
  refresh_entity_states(now_ms);
}

/* Input readings become unknown when their node stops reporting. A switch
 * cannot publish an unknown state in ESPHome, so clear its stale ON indication
 * when its previously online node disappears. */
void MECSExpansion::refresh_entity_states(uint32_t now_ms) {
  for (const auto &input : inputs_) {
    const mecs_client_node_t *node = mecs_client_node(&client_, input.node);
    const bool online = node && mecs_client_node_online(node, now_ms);
    if (!online && input.sensor->has_state()) {
      input.sensor->invalidate_state();
    }
  }

  for (auto &binding : outputs_) {
    const mecs_client_node_t *node =
        mecs_client_node(&client_, binding.output->node());
    const bool online = node && mecs_client_node_online(node, now_ms);
    if (!online && binding.was_online) {
      binding.output->publish_state(false);
      ESP_LOGW(TAG, "Output node %u is offline; clearing stale ON state",
               binding.output->node());
    }
    binding.was_online = online;
  }
}

void MECSExpansion::on_frame(uint32_t id, bool extended, bool remote,
                            const std::vector<uint8_t> &data) {
  if (extended || remote || data.size() > 8) return;
  mecs_frame_t frame{};
  frame.id = id;
  frame.length = data.size();
  frame.extended = extended;
  frame.remote = remote;
  for (size_t i = 0; i < data.size(); ++i) frame.data[i] = data[i];
  mecs_client_receive(&client_, &frame, millis());
}

void MECSExpansion::on_event(void *context, mecs_client_event_t event,
                            uint8_t node, uint8_t channel,
                            uint8_t property, uint32_t value,
                            mecs_error_t error) {
  auto *self = static_cast<MECSExpansion *>(context);
  if (event == MECS_CLIENT_EVENT_NODE_ANNOUNCED) {
    ESP_LOGI(TAG, "Node %u announced board type %u", node, (unsigned)value);
  } else if (event == MECS_CLIENT_EVENT_NODE_STATUS) {
    for (const auto &input : self->inputs_) {
      if (input.node == node) {
        input.sensor->publish_state((value & (1u << input.channel)) != 0);
      }
    }
    const mecs_client_node_t *state = mecs_client_node(&self->client_, node);
    if (state && state->identity.board_type == MECS_BOARD_DO4) {
      for (const auto &binding : self->outputs_) {
        auto *output = binding.output;
        if (output->node() == node) {
          output->publish_state((value & (1u << output->channel())) != 0);
        }
      }
    }
  } else if (event == MECS_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MECS_CLIENT_EVENT_REQUEST_TIMEOUT) {
    ESP_LOGW(TAG, "Node %u channel %u request failed (%s)", node, channel,
             mecs_error_name(error));
  } else if (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED &&
             property == MECS_PROP_VALUE) {
    for (const auto &binding : self->outputs_) {
      auto *output = binding.output;
      if (output->node() == node && output->channel() == channel) {
        output->publish_state(value != 0);
      }
    }
  }
}

bool MECSExpansion::set_output(uint8_t node, uint8_t channel, bool enabled) {
  return mecs_client_set(&client_, node, channel, MECS_PROP_VALUE,
                        enabled ? 1u : 0u);
}

void MECSOutputSwitch::write_state(bool state) {
  if (parent_ == nullptr || !parent_->set_output(node_, channel_, state)) {
    ESP_LOGW(TAG, "Output request could not be queued for node %u channel %u",
             node_, channel_);
  }
}

}  // namespace esphome::mecs_expansion
