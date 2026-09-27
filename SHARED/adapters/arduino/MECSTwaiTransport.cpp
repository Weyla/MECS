#include "MECSTwaiTransport.h"

#include "driver/twai.h"

bool MECSTwaiTransport::begin(int tx_gpio, int rx_gpio, uint32_t bitrate) {
  if (started_) {
    return false;
  }

  twai_timing_config_t timing;
  switch (bitrate) {
  case 250000:
    timing = TWAI_TIMING_CONFIG_250KBITS();
    break;
  case 500000:
    timing = TWAI_TIMING_CONFIG_500KBITS();
    break;
  case 1000000:
    timing = TWAI_TIMING_CONFIG_1MBITS();
    break;
  default:
    return false;
  }

  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(tx_gpio), static_cast<gpio_num_t>(rx_gpio),
      TWAI_MODE_NORMAL);
  // The protocol owns retries/deadlines. A driver backlog must not deliver
  // old output commands later when an unplugged node returns.
  general.tx_queue_len = 0;
  general.rx_queue_len = 16;
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
    return false;
  }
  if (twai_start() != ESP_OK) {
    (void)twai_driver_uninstall();
    return false;
  }
  started_ = true;
  return true;
}

bool MECSTwaiTransport::send(const mecs_frame_t *frame) {
  if (!started_ || !frame || frame->length > 8) {
    return false;
  }
  twai_message_t message = {};
  message.identifier = frame->id;
  message.data_length_code = frame->length;
  message.extd = frame->extended;
  message.rtr = frame->remote;
  message.ss = 1; // One attempt; the protocol retries unacknowledged requests.
  for (uint8_t i = 0; i < frame->length; ++i) {
    message.data[i] = frame->data[i];
  }
  return twai_transmit(&message, 0) == ESP_OK;
}

bool MECSTwaiTransport::receive(mecs_frame_t *frame) {
  if (!started_ || !frame) {
    return false;
  }
  twai_message_t message = {};
  if (twai_receive(&message, 0) != ESP_OK) {
    return false;
  }
  *frame = {};
  frame->id = message.identifier;
  frame->length = message.data_length_code;
  frame->extended = message.extd;
  frame->remote = message.rtr;
  for (uint8_t i = 0; i < frame->length && i < 8; ++i) {
    frame->data[i] = message.data[i];
  }
  return true;
}

void MECSTwaiTransport::end() {
  if (started_) {
    (void)twai_stop();
    (void)twai_driver_uninstall();
    started_ = false;
  }
}

bool MECSTwaiTransport::poll() {
  if (!started_) {
    return false;
  }
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) {
    return false;
  }
  if (status.state == TWAI_STATE_BUS_OFF && !recovering_) {
    /* Recovery discards the driver's old transmit queue. Reconfiguration is
     * done by the client after recovery, never by replaying old CAN frames. */
    recovering_ = twai_initiate_recovery() == ESP_OK;
  } else if (recovering_ && status.state == TWAI_STATE_STOPPED &&
             twai_start() == ESP_OK) {
    recovering_ = false;
    return true;
  }
  return false;
}
