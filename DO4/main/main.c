/*
 * DO4 output-node application.
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
#include "mio.h"
#include "mio_can.h"
#include "mio_io.h"
#include "output_hardware.h"
#include "sdkconfig.h"

#ifndef CONFIG_MIO_OUTPUT_ACTIVE_LOW
#define CONFIG_MIO_OUTPUT_ACTIVE_LOW 0
#endif

/* Board-specific identity and wiring live here, beside this module's app. */
#define NODE_ADDRESS CONFIG_MIO_NODE_ID
#define BOARD_TYPE MIO_BOARD_DO4
#define FIRMWARE_MAJOR 0
#define FIRMWARE_MINOR 3
#define FIRMWARE_PATCH 0
#define CAN_TX_GPIO 4
#define CAN_RX_GPIO 5
static const uint8_t OUTPUT_PINS[MIO_CHANNELS] = {0, 1, 2, 3};
static const char *TAG = "DO4";

/* Each object has one owner: this main task. Shared callbacks send through the
 * ESP-IDF CAN adapter and update the portable I/O model. */
static mio_t protocol;
static mio_io_node_t outputs;

static uint32_t milliseconds(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

/* A master discovery request is deliberately handled in this board's code:
 * the shared checker recognizes the request, and announce() uses this board's
 * identity previously supplied to mio_init(). */
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
    mio_io_node_heartbeat(&outputs, session, now);
    return;
  }

  mio_io_request_t request;
  if (mio_io_decode_request(NODE_ADDRESS, frame, &request)) {
    mio_io_reply_t reply = mio_io_node_request(&outputs, &request, now);
    mio_io_encode_reply(NODE_ADDRESS, &reply, reply_frame);
    *reply_pending = true;
  }
}

void app_main(void) {
  /* This module supplies its own identity. The shared announce function
   * always serializes these exact fields into this node's CAN message. */
  const mio_identity_t identity = {
      .node_id = NODE_ADDRESS,
      .board_type = BOARD_TYPE,
      .firmware_major = FIRMWARE_MAJOR,
      .firmware_minor = FIRMWARE_MINOR,
      .firmware_patch = FIRMWARE_PATCH,
  };

  mio_io_node_init(&outputs, BOARD_TYPE, OUTPUT_PINS,
                   CONFIG_MIO_OUTPUT_ACTIVE_LOW, do4_apply_output, &outputs);

  /* Start the hardware adapter and establish the inactive state before CAN
   * can deliver a command. Pin numbers above are the DO4 board definition. */
  for (unsigned channel = 0; channel < MIO_CHANNELS; ++channel) {
    ESP_ERROR_CHECK(
        do4_apply_output(&outputs, channel, &outputs.config[channel], false)
            ? ESP_OK
            : ESP_FAIL);
  }

  ESP_ERROR_CHECK(mio_init(&protocol, identity, mio_can_send, NULL, NULL)
                      ? ESP_OK
                      : ESP_FAIL);
  ESP_ERROR_CHECK(mio_can_start(CAN_TX_GPIO, CAN_RX_GPIO));

  /* Announce at boot. If the driver's transmit slot is occupied, retry from
   * the main loop instead of blocking channel safety/timing work. */
  bool announce_pending = !mio_announce(&protocol, MIO_ANNOUNCE_BOOT);
  mio_announce_reason_t announce_reason = MIO_ANNOUNCE_BOOT;
  bool reply_pending = false;
  mio_frame_t reply_frame = {0};
  uint16_t boot_marker = (uint16_t)esp_random();
  uint16_t status_sequence = 0;
  uint32_t last_status = 0;
  uint32_t last_announce = milliseconds();
  uint8_t previous_outputs = 0;
  TickType_t next_tick = xTaskGetTickCount();

  ESP_LOGI(TAG, "Node %u, board DO4, channels on GPIO0..3", identity.node_id);
  while (true) {
    const uint32_t now = milliseconds();

    /* Tick the shared model before processing commands. If heartbeats have
     * stopped, the model clears every output gate and drives pins inactive. */
    mio_io_node_tick(&outputs, do4_read_pins(&outputs), now);

    if (mio_can_poll()) {
      mio_io_node_stop(&outputs);
      announce_pending = true;
      announce_reason = MIO_ANNOUNCE_RECOVERY;
    }

    /* Keep bus work in this owner task. CAN receive only copies frames into
     * a queue inside its ISR; all protocol calls happen here. */
    mio_frame_t received;
    for (unsigned budget = 0; budget < 8 && mio_can_receive(&received);
         ++budget) {
      handle_frame(&received, now, &announce_pending, &announce_reason,
                   &reply_pending, &reply_frame);
    }

    /* Send one kind of work per pass. Replies are most time-sensitive. */
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

    const uint8_t output_gates = mio_io_node_logical(&outputs);
    const bool state_changed = output_gates != previous_outputs;
    const uint32_t status_age = now - last_status;
    const bool status_due = status_age >= MIO_STATUS_MS ||
                            (outputs.report != MIO_REPORT_CHANGE &&
                             status_age >= outputs.report_ms) ||
                            (outputs.report != MIO_REPORT_PERIODIC &&
                             state_changed && status_age >= 20);

    if (!reply_pending && !announce_pending && status_due) {
      const mio_io_status_t status = {
          .raw = do4_read_pins(&outputs),
          .logical = output_gates,
          .master_alive = outputs.master_alive,
          .fault = outputs.fault,
          .boot_id = boot_marker,
          .sequence = status_sequence,
      };
      mio_frame_t status_frame;
      mio_io_encode_status(NODE_ADDRESS, &status, &status_frame);
      if (mio_can_send(NULL, &status_frame)) {
        last_status = now;
        previous_outputs = output_gates;
        ++status_sequence;
      }
    }

    vTaskDelayUntil(&next_tick, pdMS_TO_TICKS(1));
  }
}
