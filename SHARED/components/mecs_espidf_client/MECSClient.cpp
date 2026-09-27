#include "MECSClient.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "mecs_can.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdint.h>

namespace {
constexpr char TAG[] = "mecs_client";
constexpr char SESSION_NAMESPACE[] = "mecs";
constexpr char SESSION_KEY[] = "session";
constexpr char LEGACY_NAMESPACE[] = "mio";

esp_err_t nextSession(uint16_t *session) {
  if (!session) return ESP_ERR_INVALID_ARG;

  esp_err_t error = nvs_flash_init();
  if (error != ESP_OK) return error;

  nvs_handle_t storage;
  error = nvs_open(SESSION_NAMESPACE, NVS_READWRITE, &storage);
  if (error != ESP_OK) return error;

  uint16_t previous = 0;
  error = nvs_get_u16(storage, SESSION_KEY, &previous);
  if (error == ESP_ERR_NVS_NOT_FOUND) {
    /* One-time read of the old namespace prevents a session collision after
     * updating firmware while an expansion node remains powered. */
    nvs_handle_t legacyStorage;
    const esp_err_t opened =
        nvs_open(LEGACY_NAMESPACE, NVS_READONLY, &legacyStorage);
    if (opened == ESP_OK) {
      error = nvs_get_u16(legacyStorage, SESSION_KEY, &previous);
      nvs_close(legacyStorage);
    } else if (opened == ESP_ERR_NVS_NOT_FOUND) {
      error = ESP_OK;
    } else {
      error = opened;
    }
  }
  if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;

  if (error == ESP_OK) {
    const uint16_t next = previous == UINT16_MAX ? 1 : previous + 1;
    error = nvs_set_u16(storage, SESSION_KEY, next);
    if (error == ESP_OK) error = nvs_commit(storage);
    if (error == ESP_OK) *session = next;
  }
  nvs_close(storage);
  return error;
}
} // namespace

esp_err_t MECSClient::begin(int txGpio, int rxGpio, uint32_t bitrate) {
  if (started_) return ESP_ERR_INVALID_STATE;
  if (txGpio == rxGpio || !bitrate) return ESP_ERR_INVALID_ARG;

  uint16_t session = 0;
  esp_err_t error = nextSession(&session);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Could not save the master session: %s",
             esp_err_to_name(error));
    return error;
  }

  error = mecs_can_start_with_bitrate(txGpio, rxGpio, bitrate);
  if (error != ESP_OK) return error;

  mecs_client_config_t config{};
  config.send_frame = mecs_can_send;
  config.transport_context = nullptr;
  config.session = session;
  if (!MECSController::start(config)) return ESP_FAIL;

  started_ = true;
  ESP_LOGI(TAG, "MECS ready; master session %u", session);
  return ESP_OK;
}

void MECSClient::loop() {
  if (!started_) return;

  const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (mecs_can_poll()) transportRecovered();

  mecs_frame_t frame;
  for (unsigned count = 0; count < 24 && mecs_can_receive(&frame); ++count)
    receive(frame, nowMs);

  if (needsNewSession() &&
      (!attemptedSessionRenewal_ ||
       static_cast<uint32_t>(nowMs - lastSessionRenewalMs_) >= 1000)) {
    attemptedSessionRenewal_ = true;
    lastSessionRenewalMs_ = nowMs;
    uint16_t session = 0;
    const esp_err_t error = nextSession(&session);
    if (error == ESP_OK) {
      if (setSession(session)) attemptedSessionRenewal_ = false;
    } else {
      ESP_LOGE(TAG, "Could not save a renewed master session: %s",
               esp_err_to_name(error));
    }
  }

  service(nowMs);
}

void MECSClient::enableLogging() { setLogger(logEvent, nullptr); }

void MECSClient::disableLogging() { setLogger(nullptr, nullptr); }

void MECSClient::logEvent(void *, mecs_client_event_t event, uint8_t node,
                          uint8_t channel, uint8_t property, uint32_t value,
                          mecs_error_t error) {
  if (event == MECS_CLIENT_EVENT_NODE_ANNOUNCED) {
    ESP_LOGI(TAG, "Node %u announced, board type %lu", node,
             (unsigned long)value);
  } else if (event == MECS_CLIENT_EVENT_REQUEST_CONFIRMED) {
    if (property >= MECS_PROP_COUNT) return;
    const mecs_property_info_t &info = mecs_properties[property];
    const unsigned long scale = info.scale;
    if (scale == 1000) {
      ESP_LOGI(TAG, "Node %u channel %u %s confirmed: %lu.%03lu %s", node,
               channel, info.name, (unsigned long)(value / scale),
               (unsigned long)(value % scale), info.unit);
    } else if (scale == 100) {
      ESP_LOGI(TAG, "Node %u channel %u %s confirmed: %lu.%02lu %s", node,
               channel, info.name, (unsigned long)(value / scale),
               (unsigned long)(value % scale), info.unit);
    } else if (scale == 10) {
      ESP_LOGI(TAG, "Node %u channel %u %s confirmed: %lu.%01lu %s", node,
               channel, info.name, (unsigned long)(value / scale),
               (unsigned long)(value % scale), info.unit);
    } else {
      ESP_LOGI(TAG, "Node %u channel %u %s confirmed: %lu %s", node, channel,
               info.name, (unsigned long)value, info.unit);
    }
  } else if (event == MECS_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MECS_CLIENT_EVENT_REQUEST_TIMEOUT) {
    ESP_LOGW(TAG, "Node %u channel %u request failed: %s", node, channel,
             mecs_error_name(error));
  }
}
