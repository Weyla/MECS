/*
 * Customer master helper for the DI4 input expansion.
 * The ESP-IDF client component is fetched from https://github.com/Weyla/MECS
 * using the idf_component.yml manifest in the companion example project.
 *
 * Remote DI4 channel-to-pin map: 0->GPIO0, 1->GPIO1, 2->GPIO2, 3->GPIO3.
 * Master CAN wiring: TX GPIO4, RX GPIO5, 500 kbit/s.
 * Start it with mio_can_start(CAN_TX_GPIO, CAN_RX_GPIO), use mio_can_send as
 * the client's send_frame callback, and poll with mio_espidf_client_poll().
 * Call this after DI4 announces; each true return means the request was queued,
 * not yet applied. Check request-confirmed/rejected/timeout events.
 */
#include "mio_client.h"

#define DI4_NODE_ID 2u
#define CAN_TX_GPIO 4
#define CAN_RX_GPIO 5
#define CAN_BITRATE 500000u
#define DI4_CHANNEL_0_GPIO 0
#define DI4_CHANNEL_1_GPIO 1
#define DI4_CHANNEL_2_GPIO 2
#define DI4_CHANNEL_3_GPIO 3

bool configure_di4_inputs(mio_client_t *client) {
  /* Active polarity: 0=high, 1=low. */
  if (!mio_client_set(client, DI4_NODE_ID, 0, MIO_PROP_POLARITY, 1) ||
      !mio_client_set(client, DI4_NODE_ID, 1, MIO_PROP_POLARITY, 0) ||
      !mio_client_set(client, DI4_NODE_ID, 2, MIO_PROP_POLARITY, 1) ||
      !mio_client_set(client, DI4_NODE_ID, 3, MIO_PROP_POLARITY, 1)) {
    return false;
  }

  /* No filter=0, stable debounce=1, PWM measurement=2. */
  if (!mio_client_set(client, DI4_NODE_ID, 0, MIO_PROP_FILTER,
                      MIO_FILTER_STABLE) ||
      !mio_client_set(client, DI4_NODE_ID, 1, MIO_PROP_FILTER,
                      MIO_FILTER_NONE) ||
      !mio_client_set(client, DI4_NODE_ID, 2, MIO_PROP_FILTER,
                      MIO_FILTER_PWM) ||
      !mio_client_set(client, DI4_NODE_ID, 3, MIO_PROP_FILTER,
                      MIO_FILTER_STABLE)) {
    return false;
  }

  /* Debounce duration range is 1..1000 ms. */
  if (!mio_client_set(client, DI4_NODE_ID, 0, MIO_PROP_FILTER_MS, 20) ||
      !mio_client_set(client, DI4_NODE_ID, 3, MIO_PROP_FILTER_MS, 20)) {
    return false;
  }

  /* Pull: none=0, up=1, down=2. */
  if (!mio_client_set(client, DI4_NODE_ID, 0, MIO_PROP_PULL, MIO_PULL_UP) ||
      !mio_client_set(client, DI4_NODE_ID, 1, MIO_PROP_PULL, MIO_PULL_DOWN) ||
      !mio_client_set(client, DI4_NODE_ID, 2, MIO_PROP_PULL, MIO_PULL_UP) ||
      !mio_client_set(client, DI4_NODE_ID, 3, MIO_PROP_PULL, MIO_PULL_UP)) {
    return false;
  }

  /* Reporting is node-wide: both=2; the interval range is 20..500 ms. */
  return mio_client_set(client, DI4_NODE_ID, MIO_GLOBAL_CHANNEL,
                        MIO_PROP_REPORT, MIO_REPORT_BOTH) &&
         mio_client_set(client, DI4_NODE_ID, MIO_GLOBAL_CHANNEL,
                        MIO_PROP_REPORT_MS, 100);
}
