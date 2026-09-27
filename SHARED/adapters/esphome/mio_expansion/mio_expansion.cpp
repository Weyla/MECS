#include "mio_expansion.h"

#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome::mio_expansion {

static const char *const TAG = "mio_expansion";
static constexpr uint32_t SESSION_PREFERENCE_KEY = 0x4D494F31u;

void MIOExpansion::add_input(uint8_t node, uint8_t channel,
                             binary_sensor::BinarySensor *sensor) {
  inputs_.push_back({node, channel, sensor});
}

bool MIOExpansion::send_frame(void *context, const mio_frame_t *frame) {
  auto *self = static_cast<MIOExpansion *>(context);
  std::vector<uint8_t> data(frame->data, frame->data + frame->length);
  return self->canbus_->send_data(frame->id, frame->extended, frame->remote,
                                  data) == canbus::ERROR_OK;
}

void MIOExpansion::setup() {
  if (canbus_ == nullptr) {
    ESP_LOGE(TAG, "A CAN bus is required");
    mark_failed();
    return;
  }
  /* Persist a new session on each boot so a rebooted master cannot accidentally
   * continue an output-enable lease from an earlier run. */
  auto preference = global_preferences->make_preference<uint16_t>(
      SESSION_PREFERENCE_KEY, true);
  uint16_t previous_session = 0;
  (void)preference.load(&previous_session);
  session_ = previous_session == 0xFFFFu ? 1u : previous_session + 1u;
  if (session_ == 0) session_ = 1;
  if (!preference.save(&session_)) {
    ESP_LOGE(TAG, "Could not save master session");
    mark_failed();
    return;
  }
  global_preferences->sync();
  const mio_client_config_t config = {send_frame, this, session_};
  if (!mio_client_begin(&client_, &config, on_event, this)) {
    ESP_LOGE(TAG, "MIO client initialization failed");
    mark_failed();
    return;
  }
  canbus_->add_callback([this](uint32_t id, bool extended, bool remote,
                               const std::vector<uint8_t> &data) {
    this->on_frame(id, extended, remote, data);
  });
  ESP_LOGI(TAG, "MIO expansion client ready");
}

void MIOExpansion::loop() {
  if (is_failed()) return;
  mio_client_loop(&client_, millis());
}

void MIOExpansion::on_frame(uint32_t id, bool extended, bool remote,
                            const std::vector<uint8_t> &data) {
  if (extended || remote || data.size() > 8) return;
  mio_frame_t frame{};
  frame.id = id;
  frame.length = data.size();
  frame.extended = extended;
  frame.remote = remote;
  for (size_t i = 0; i < data.size(); ++i) frame.data[i] = data[i];
  mio_client_receive(&client_, &frame, millis());
}

void MIOExpansion::on_event(void *context, mio_client_event_t event,
                            uint8_t node, uint8_t channel,
                            uint8_t property, uint16_t value,
                            mio_error_t error) {
  auto *self = static_cast<MIOExpansion *>(context);
  if (event == MIO_CLIENT_EVENT_NODE_ANNOUNCED) {
    ESP_LOGI(TAG, "Node %u announced board type %u", node, value);
  } else if (event == MIO_CLIENT_EVENT_NODE_STATUS) {
    for (const auto &input : self->inputs_) {
      if (input.node == node) {
        input.sensor->publish_state((value & (1u << input.channel)) != 0);
      }
    }
    const mio_client_node_t *state = mio_client_node(&self->client_, node);
    if (state && state->identity.board_type == MIO_BOARD_DO4) {
      for (auto *output : self->outputs_) {
        if (output->node() == node) {
          output->publish_state((value & (1u << output->channel())) != 0);
        }
      }
    }
  } else if (event == MIO_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MIO_CLIENT_EVENT_REQUEST_TIMEOUT) {
    ESP_LOGW(TAG, "Node %u channel %u request failed (%s)", node, channel,
             mio_error_name(error));
  } else if (event == MIO_CLIENT_EVENT_REQUEST_CONFIRMED &&
             property == MIO_PROP_VALUE) {
    for (auto *output : self->outputs_) {
      if (output->node() == node && output->channel() == channel) {
        output->publish_state(value != 0);
      }
    }
  }
}

bool MIOExpansion::set_output(uint8_t node, uint8_t channel, bool enabled) {
  return mio_client_set(&client_, node, channel, MIO_PROP_VALUE,
                        enabled ? 1u : 0u);
}

void MIOOutputSwitch::write_state(bool state) {
  if (parent_ == nullptr || !parent_->set_output(node_, channel_, state)) {
    ESP_LOGW(TAG, "Output request could not be queued for node %u channel %u",
             node_, channel_);
  }
}

}  // namespace esphome::mio_expansion
