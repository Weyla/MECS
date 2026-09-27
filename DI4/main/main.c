/*
 * DI4 input-node application.
 *
 * This file is the board's wiring and identity sheet, followed by the small
 * owner loop that connects shared protocol/I/O functions to the CAN driver.
 * Start here when learning how this particular module behaves.
 */
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input_hardware.h"
#include "mio.h"
#include "mio_can.h"
#include "mio_io.h"
#include "pwm_measurement.h"
#include "sdkconfig.h"

/* Board-specific identity and wiring live here, beside this module's app. */
#define NODE_ADDRESS CONFIG_MIO_NODE_ID
#define BOARD_TYPE MIO_BOARD_DI4
#define FIRMWARE_MAJOR 0
#define FIRMWARE_MINOR 3
#define FIRMWARE_PATCH 0
#define CAN_TX_GPIO 4
#define CAN_RX_GPIO 5
static const uint8_t INPUT_PINS[MIO_CHANNELS] = {0, 1, 2, 3};
static const char *TAG = "DI4";

static mio_t protocol;
static mio_io_node_t inputs;

static uint32_t milliseconds(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

/* A master discovery request is deliberately handled in this board's code.
 * The announce call uses the DI4 identity configured in app_main(). */
static void handle_frame(const mio_frame_t *frame, uint32_t now,
                         bool *announce_pending,
                         mio_announce_reason_t *announce_reason,
                         bool *reply_pending, mio_frame_t *reply_frame) {
  if (mio_is_discovery_request(frame)) {
    *announce_pending = true;
    *announce_reason = MIO_ANNOUNCE_REQUEST;
    return;
  }

  uint16_t session;
  if (mio_io_decode_heartbeat(frame, &session)) {
    mio_io_node_heartbeat(&inputs, session, now);
    return;
  }

  mio_io_request_t request;
  if (mio_io_decode_request(NODE_ADDRESS, frame, &request)) {
    mio_io_reply_t reply = mio_io_node_request(&inputs, &request, now);
    mio_io_encode_reply(NODE_ADDRESS, &reply, reply_frame);
    *reply_pending = true;
  }
}

void app_main(void) {
  /* This module supplies its own identity; the shared discovery library
   * serializes these exact fields when this app calls mio_announce(). */
  const mio_identity_t identity = {
      .node_id = NODE_ADDRESS,
      .board_type = BOARD_TYPE,
      .firmware_major = FIRMWARE_MAJOR,
      .firmware_minor = FIRMWARE_MINOR,
      .firmware_patch = FIRMWARE_PATCH,
  };

  mio_io_node_init(&inputs, BOARD_TYPE, INPUT_PINS, false, di4_apply_input,
                   &inputs);
  ESP_ERROR_CHECK(di4_hardware_start(&inputs) ? ESP_OK : ESP_FAIL);
  for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
    ESP_ERROR_CHECK(
        di4_apply_input(&inputs, channel, &inputs.config[channel], false)
            ? ESP_OK
            : ESP_FAIL);
  }

  ESP_ERROR_CHECK(mio_init(&protocol, identity, mio_can_send, NULL, NULL)
                      ? ESP_OK
                      : ESP_FAIL);
  ESP_ERROR_CHECK(mio_can_start(CAN_TX_GPIO, CAN_RX_GPIO));

  bool announce_pending = !mio_announce(&protocol, MIO_ANNOUNCE_BOOT);
  mio_announce_reason_t announce_reason = MIO_ANNOUNCE_BOOT;
  bool reply_pending = false;
  mio_frame_t reply_frame = {0};
  uint16_t boot_marker = (uint16_t)esp_random();
  uint16_t status_sequence = 0;
  uint32_t last_status = 0;
  uint32_t last_announce = milliseconds();
  uint8_t previous_inputs = 0;
  uint8_t measurement_pending = 0;
  TickType_t next_tick = xTaskGetTickCount();

  ESP_LOGI(TAG, "Node %u, board DI4, channels on GPIO0..3", identity.node_id);
  while (true) {
    const uint32_t now = milliseconds();
    const uint8_t raw_levels = di4_read_pins(&inputs);
    mio_io_node_tick(&inputs, raw_levels, now);

    if (mio_can_poll()) {
      announce_pending = true;
      announce_reason = MIO_ANNOUNCE_RECOVERY;
    }

    mio_frame_t received;
    for (unsigned budget = 0; budget < 8 && mio_can_receive(&received);
         ++budget) {
      handle_frame(&received, now, &announce_pending, &announce_reason,
                   &reply_pending, &reply_frame);
    }

    if (reply_pending && mio_can_send(NULL, &reply_frame)) {
      reply_pending = false;
    }
    if (!reply_pending && announce_pending &&
        mio_announce(&protocol, announce_reason)) {
      announce_pending = false;
      last_announce = now;
    }
    if ((uint32_t)(now - last_announce) >= 5000) {
      announce_pending = true;
      announce_reason = MIO_ANNOUNCE_REQUEST;
    }

    const uint8_t logical_inputs = mio_io_node_logical(&inputs);
    const bool state_changed = logical_inputs != previous_inputs;
    const uint32_t status_age = now - last_status;
    const bool status_due = status_age >= MIO_STATUS_MS ||
                            (inputs.report != MIO_REPORT_CHANGE &&
                             status_age >= inputs.report_ms) ||
                            (inputs.report != MIO_REPORT_PERIODIC &&
                             state_changed && status_age >= 20);

    if (!reply_pending && !announce_pending && status_due) {
      const mio_io_status_t status = {
          .raw = raw_levels,
          .logical = logical_inputs,
          .master_alive = inputs.master_alive,
          .fault = inputs.fault,
          .boot_id = boot_marker,
          .sequence = status_sequence,
      };
      mio_frame_t status_frame;
      mio_io_encode_status(NODE_ADDRESS, &status, &status_frame);
      if (mio_can_send(NULL, &status_frame)) {
        last_status = now;
        previous_inputs = logical_inputs;
        ++status_sequence;
        /* Send fresh measurements after the liveness/status frame. */
        for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
          if (inputs.config[channel].filter == MIO_FILTER_PWM) {
            measurement_pending |= (uint8_t)(1u << channel);
          }
        }
      }
    }

    /* Only configured PWM inputs produce measurement frames. Capture is
     * local to this board; CAN carries the latest period/duty, not edges. */
    for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
      if (inputs.config[channel].filter != MIO_FILTER_PWM) {
        measurement_pending &= (uint8_t)~(1u << channel);
        continue;
      }
      const mio_pwm_measurement_t measured =
          di4_measure_pwm((uint8_t)channel, inputs.config[channel].active_low);
      if (!reply_pending && !announce_pending &&
          (measurement_pending & (1u << channel))) {
        mio_frame_t measurement_frame;
        mio_io_encode_measurement(NODE_ADDRESS, (uint8_t)channel, &measured,
                                  &measurement_frame);
        if (mio_can_send(NULL, &measurement_frame)) {
          measurement_pending &= (uint8_t)~(1u << channel);
        }
      }
    }

    vTaskDelayUntil(&next_tick, pdMS_TO_TICKS(1));
  }
}
