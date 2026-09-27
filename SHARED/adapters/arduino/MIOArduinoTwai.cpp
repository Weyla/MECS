#include "MIOArduinoTwai.h"

#include "driver/twai.h"

bool MIOArduinoTwai::begin(int tx_gpio, int rx_gpio, uint32_t bitrate) {
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
  general.tx_queue_len = 8;
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

bool MIOArduinoTwai::send(const mio_frame_t *frame) {
  if (!started_ || !frame || frame->length > 8) {
    return false;
  }
  twai_message_t message = {};
  message.identifier = frame->id;
  message.data_length_code = frame->length;
  message.extd = frame->extended;
  message.rtr = frame->remote;
  for (uint8_t i = 0; i < frame->length; ++i) {
    message.data[i] = frame->data[i];
  }
  return twai_transmit(&message, 0) == ESP_OK;
}

bool MIOArduinoTwai::receive(mio_frame_t *frame) {
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

void MIOArduinoTwai::end() {
  if (started_) {
    (void)twai_stop();
    (void)twai_driver_uninstall();
    started_ = false;
  }
}
