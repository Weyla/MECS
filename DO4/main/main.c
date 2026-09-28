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
#include "mecs_protocol.h"
#include "mecs_can.h"
#include "mecs_io.h"
#include "output_hardware.h"
#include "sdkconfig.h"

#ifndef CONFIG_MECS_OUTPUT_ACTIVE_LOW
#define CONFIG_MECS_OUTPUT_ACTIVE_LOW 0
#endif

/* Board-specific identity and wiring live here, beside this module's app. */
#define NODE_ADDRESS CONFIG_MECS_NODE_ID
#define BOARD_TYPE MECS_BOARD_DO4
#define FIRMWARE_MAJOR 0
#define FIRMWARE_MINOR 3
#define FIRMWARE_PATCH 1
#define CAN_TX_GPIO 4
#define CAN_RX_GPIO 5
static const uint8_t OUTPUT_PINS[MECS_CHANNELS] = {0, 1, 2, 3};
static const char *TAG = "DO4";

/* Each object has one owner: this main task. Shared callbacks send through the
 * ESP-IDF CAN adapter and update the portable I/O model. */
static mecs_t protocol;
static mecs_io_node_t outputs;

static uint32_t milliseconds(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

/* A master discovery request is deliberately handled in this board's code:
 * the shared checker recognizes the request, and announce() uses this board's
 * identity previously supplied to mecs_init(). */
static void handle_frame(const mecs_frame_t *frame, uint32_t now,
                         bool *announce_pending,
                         mecs_announce_reason_t *announce_reason,
                         bool *reply_pending, mecs_frame_t *reply_frame) {
  if (mecs_is_discovery_request(frame)) {
    if (!*announce_pending) *announce_reason = MECS_ANNOUNCE_REQUEST;
    *announce_pending = true;
    return;
  }

  uint16_t session;
  if (mecs_io_decode_heartbeat(frame, &session)) {
    if (!outputs.master_alive || session != outputs.session) {
      ESP_LOGI(TAG, "Master heartbeat: session %u -> %u", outputs.session, session);
    }
    mecs_io_node_heartbeat(&outputs, session, now);
    return;
  }

  mecs_io_request_t request;
  if (mecs_io_decode_request(NODE_ADDRESS, frame, &request)) {
    /* Continue draining broadcasts while TX is busy. A command whose reply
     * cannot be retained is left for the master's bounded retry. */
    if (*reply_pending) {
      ESP_LOGD(TAG, "Command deferred: reply slot busy, transaction=%u", request.transaction);
      return;
    }
    mecs_io_reply_t reply = mecs_io_node_request(&outputs, &request, now);
    static bool rejection_logged;
    static uint32_t last_rejection_ms;
    if (reply.error != MECS_OK &&
        (!rejection_logged || (uint32_t)(now - last_rejection_ms) >= 1000)) {
      ESP_LOGW(TAG, "Rejected session=%u transaction=%u channel=%u property=0x%02x: %s",
               request.session, request.transaction, request.channel,
               request.property, mecs_error_name(reply.error));
      rejection_logged = true;
      last_rejection_ms = now;
    }
    ESP_LOGD(TAG, "Request session=%u transaction=%u channel=%u property=0x%02x value=%lu: %s",
             request.session, request.transaction, request.channel, request.property,
             (unsigned long)request.value, mecs_error_name(reply.error));
    mecs_io_encode_reply(NODE_ADDRESS, &reply, reply_frame);
    *reply_pending = true;
  }
}

void app_main(void) {
  /* This module supplies its own identity. The shared announce function
   * always serializes these exact fields into this node's CAN message. */
  const mecs_identity_t identity = {
      .node_id = NODE_ADDRESS,
      .board_type = BOARD_TYPE,
      .firmware_major = FIRMWARE_MAJOR,
      .firmware_minor = FIRMWARE_MINOR,
      .firmware_patch = FIRMWARE_PATCH,
  };

  mecs_io_node_init(&outputs, BOARD_TYPE, OUTPUT_PINS,
                   CONFIG_MECS_OUTPUT_ACTIVE_LOW, do4_apply_output, &outputs);

  /* Start the hardware adapter and establish the inactive state before CAN
   * can deliver a command. Pin numbers above are the DO4 board definition. */
  for (unsigned channel = 0; channel < MECS_CHANNELS; ++channel) {
    ESP_ERROR_CHECK(
        do4_apply_output(&outputs, channel, &outputs.config[channel], false)
            ? ESP_OK
            : ESP_FAIL);
  }

  ESP_ERROR_CHECK(mecs_init(&protocol, identity, mecs_can_send, NULL, NULL)
                      ? ESP_OK
                      : ESP_FAIL);
  ESP_ERROR_CHECK(mecs_can_start(CAN_TX_GPIO, CAN_RX_GPIO));

  /* Announce at boot. If the driver's transmit slot is occupied, retry from
   * the main loop instead of blocking channel safety/timing work. */
  bool announce_pending = !mecs_announce(&protocol, MECS_ANNOUNCE_BOOT);
  mecs_announce_reason_t announce_reason = MECS_ANNOUNCE_BOOT;
  bool reply_pending = false;
  mecs_frame_t reply_frame = {0};
  uint16_t boot_marker = (uint16_t)esp_random();
  uint16_t status_sequence = 0;
  uint32_t last_status = 0;
  uint32_t last_announce = milliseconds();
  uint8_t previous_outputs = 0;
  bool previous_master_alive = false;
  bool previous_fault = false;
  TickType_t next_tick = xTaskGetTickCount();

  ESP_LOGI(TAG, "Node %u, board DO4, channels on GPIO0..3", identity.node_id);
  while (true) {
    const uint32_t now = milliseconds();

    /* Tick the shared model before processing commands. If heartbeats have
     * stopped, the model clears every output gate and drives pins inactive. */
    mecs_io_node_tick(&outputs, do4_read_pins(&outputs), now);

    if (previous_master_alive && !outputs.master_alive) {
      ESP_LOGW(TAG, "Master lease expired; session=%u", outputs.session);
    }

    if (mecs_can_poll()) {
      mecs_io_node_stop(&outputs);
      reply_pending = false;
      announce_pending = true;
      announce_reason = MECS_ANNOUNCE_RECOVERY;
    }

    /* Keep bus work in this owner task. CAN receive only copies frames into
     * a queue inside its ISR; all protocol calls happen here. */
    mecs_frame_t received;
    for (unsigned budget = 0; budget < 8 && mecs_can_receive(&received);
         ++budget) {
      handle_frame(&received, now, &announce_pending, &announce_reason,
                   &reply_pending, &reply_frame);
    }

    if (outputs.fault && !previous_fault) {
      ESP_LOGE(TAG, "Hardware fault latched; inspect channel configuration and GPIO/LEDC errors");
    }
    previous_master_alive = outputs.master_alive;
    previous_fault = outputs.fault;

    /* Send one kind of work per pass. Replies are most time-sensitive. */
    if (reply_pending && mecs_can_send(NULL, &reply_frame)) {
      reply_pending = false;
    }
    if (!reply_pending && announce_pending &&
        mecs_announce(&protocol, announce_reason)) {
      announce_pending = false;
      last_announce = now;
    }
    if (!announce_pending && (uint32_t)(now - last_announce) >= 5000) {
      announce_pending = true;
      announce_reason = MECS_ANNOUNCE_REQUEST;
    }

    const uint8_t output_gates = mecs_io_node_logical(&outputs);
    const bool state_changed = output_gates != previous_outputs;
    const uint32_t status_age = now - last_status;
    const bool status_due = status_age >= MECS_STATUS_MS ||
                            (outputs.report != MECS_REPORT_CHANGE &&
                             status_age >= outputs.report_ms) ||
                            (outputs.report != MECS_REPORT_PERIODIC &&
                             state_changed && status_age >= outputs.report_min_ms);

    if (!reply_pending && !announce_pending && status_due) {
      const mecs_io_status_t status = {
          .raw = do4_read_pins(&outputs),
          .logical = output_gates,
          .master_alive = outputs.master_alive,
          .fault = outputs.fault,
          .boot_id = boot_marker,
          .sequence = status_sequence,
      };
      mecs_frame_t status_frame;
      mecs_io_encode_status(NODE_ADDRESS, &status, &status_frame);
      if (mecs_can_send(NULL, &status_frame)) {
        last_status = now;
        previous_outputs = output_gates;
        ++status_sequence;
      }
    }

    vTaskDelayUntil(&next_tick, pdMS_TO_TICKS(1) ? pdMS_TO_TICKS(1) : 1);
  }
}
