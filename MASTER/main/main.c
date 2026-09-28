/* Master application entry point and owner loop. Bus state/transactions,
 * serial commands and JSON formatting are kept in focused source files. */
#include "board_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "master_internal.h"
#include "mecs_protocol.h"
#include "mecs_can.h"
#include "mecs_io.h"

void app_main(void) {
  mutex = xSemaphoreCreateRecursiveMutex();
  commands = xQueueCreate(MASTER_COMMAND_QUEUE_LENGTH, sizeof(command_t));
  ESP_ERROR_CHECK(mutex && commands ? ESP_OK : ESP_ERR_NO_MEM);
  const mecs_identity_t identity = {.node_id = 0};
  ESP_ERROR_CHECK(
      mecs_init(&discovery, identity, mecs_can_send, master_discovered, NULL)
          ? ESP_OK
          : ESP_FAIL);
  ESP_ERROR_CHECK(mecs_can_start(MASTER_CAN_TX_GPIO, MASTER_CAN_RX_GPIO));
  network_start();
  session = master_next_session();
  web_start();
  master_note("MECS 0.3.1: discovery + four-channel I/O. Type help.");
  discover_pending = true;
  uint32_t last_heartbeat = master_now_ms() - MECS_HEARTBEAT_MS,
           last_discovery = master_now_ms();
  for (;;) {
    uint32_t now = master_now_ms();
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    if (mecs_can_poll()) {
      master_transport_reset();
    }
    master_service_state(now);
    mecs_frame_t frame;
    for (unsigned i = 0; i < 32 && mecs_can_receive(&frame); ++i) {
      master_accept_frame(&frame, now);
    }
    /* Heartbeat takes precedence so configuration browsing cannot starve
     * the node's master-alive lease. Retry whenever the TX slot is free. */
    if (heartbeat_pending || (uint32_t)(now - last_heartbeat) >= MECS_HEARTBEAT_MS) {
      mecs_io_encode_heartbeat(session, &frame);
      if (mecs_can_send(NULL, &frame)) {
        last_heartbeat = now;
        heartbeat_pending = false;
      }
    } else if (discover_pending || (uint32_t)(now - last_discovery) >= 5000) {
      if (mecs_request_discovery(&discovery)) {
        discover_pending = false;
        last_discovery = now;
      }
    } else {
      master_transactions(now);
    }
    mecs_can_get_stats(&can_stats);
    xSemaphoreGiveRecursive(mutex);
    master_console_poll();
    vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
  }
}
