/* Master application entry point and owner loop. Bus state/transactions,
 * serial commands and JSON formatting are kept in focused source files. */
#include "board_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "master_internal.h"
#include "mio.h"
#include "mio_can.h"
#include "mio_io.h"

void app_main(void) {
  mutex = xSemaphoreCreateRecursiveMutex();
  commands = xQueueCreate(32, sizeof(command_t));
  ESP_ERROR_CHECK(mutex && commands ? ESP_OK : ESP_ERR_NO_MEM);
  const mio_identity_t identity = {.node_id = 0};
  ESP_ERROR_CHECK(
      mio_init(&discovery, identity, mio_can_send, master_discovered, NULL)
          ? ESP_OK
          : ESP_FAIL);
  ESP_ERROR_CHECK(mio_can_start(MASTER_CAN_TX_GPIO, MASTER_CAN_RX_GPIO));
  network_start();
  session = master_next_session();
  web_start();
  master_note("MIO 0.3.0: discovery + four-channel I/O. Type help.");
  discover_pending = true;
  uint32_t last_heartbeat = master_now_ms() - MIO_HEARTBEAT_MS,
           last_discovery = master_now_ms();
  for (;;) {
    uint32_t now = master_now_ms();
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
    if (mio_can_poll()) {
      discover_pending = true;
    }
    mio_frame_t frame;
    for (unsigned i = 0; i < 32 && mio_can_receive(&frame); ++i) {
      master_accept_frame(&frame, now);
    }
    /* Heartbeat takes precedence so configuration browsing cannot starve
     * the node's master-alive lease. Retry whenever the TX slot is free. */
    if ((uint32_t)(now - last_heartbeat) >= MIO_HEARTBEAT_MS) {
      mio_io_encode_heartbeat(session, &frame);
      if (mio_can_send(NULL, &frame)) {
        last_heartbeat = now;
      }
    } else if (discover_pending || (uint32_t)(now - last_discovery) >= 5000) {
      if (mio_request_discovery(&discovery)) {
        discover_pending = false;
        last_discovery = now;
      }
    } else {
      master_transactions(now);
    }
    xSemaphoreGiveRecursive(mutex);
    master_console_poll();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
