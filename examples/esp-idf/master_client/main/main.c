/* Minimal ESP-IDF customer application using the public master client.
 * Node data and commands are handled through mio_client; the dashboard
 * application in MASTER is not part of this example or library. */
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mio_client.h"
#include "mio_espidf_client.h"

#define CAN_TX_GPIO 4
#define CAN_RX_GPIO 5
#define MASTER_SESSION 1u /* Replace with a persisted, incrementing session. */

static mio_client_t io;

static void on_mio_event(void *context, mio_client_event_t event,
                         uint8_t node_id, uint8_t channel,
                         uint8_t property, uint16_t value,
                         mio_error_t error) {
  (void)context;
  if (event == MIO_CLIENT_EVENT_NODE_ANNOUNCED) {
    printf("Node %u announced board type %u\n", node_id, value);
  } else if (event == MIO_CLIENT_EVENT_REQUEST_CONFIRMED) {
    printf("Node %u channel %u property %u confirmed: %u\n", node_id,
           channel, property, value);
  } else if (event == MIO_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MIO_CLIENT_EVENT_REQUEST_TIMEOUT) {
    printf("Node %u request did not complete (error %s)\n", node_id,
           mio_error_name(error));
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(mio_can_start(CAN_TX_GPIO, CAN_RX_GPIO));
  const mio_client_config_t config = {
      .send_frame = mio_can_send,
      .transport_context = NULL,
      .session = MASTER_SESSION,
  };
  ESP_ERROR_CHECK(mio_client_begin(&io, &config, on_mio_event, NULL)
                      ? ESP_OK
                      : ESP_FAIL);

  for (;;) {
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    (void)mio_espidf_client_poll(&io, now_ms, 24);

    /* After node 2 has announced and reported status, a customer can submit:
     * mio_client_get(&io, 2, 0, MIO_PROP_FILTER);
     * The function returns false if the node is offline or another request
     * is still awaiting acknowledgement. Check the callback before proceeding. */
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
