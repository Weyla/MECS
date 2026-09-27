/*
 * Customer master helper for the DO4 output expansion.
 * The ESP-IDF client component is fetched from https://github.com/Weyla/MECS
 * using the idf_component.yml manifest in the companion example project.
 *
 * Remote DO4 channel-to-pin map: 0->GPIO0, 1->GPIO1, 2->GPIO2, 3->GPIO3.
 * Master CAN wiring: TX GPIO4, RX GPIO5, 500 kbit/s.
 * Start it with mio_can_start(CAN_TX_GPIO, CAN_RX_GPIO), use mio_can_send as
 * the client's send_frame callback, and poll with mio_espidf_client_poll().
 * Call after DO4 announces. Queue acceptance is not confirmation; observe the
 * request events before treating settings as applied.
 */
#include "mio_client.h"

#define DO4_NODE_ID 1u
#define CAN_TX_GPIO 4
#define CAN_RX_GPIO 5
#define CAN_BITRATE 500000u
#define DO4_CHANNEL_0_GPIO 0
#define DO4_CHANNEL_1_GPIO 1
#define DO4_CHANNEL_2_GPIO 2
#define DO4_CHANNEL_3_GPIO 3

bool configure_do4_outputs(mio_client_t *client) {
  /* Polarity: 0=active-high, 1=active-low. */
  for (uint8_t channel = 0; channel < MIO_CHANNELS; ++channel) {
    if (!mio_client_set(client, DO4_NODE_ID, channel, MIO_PROP_POLARITY, 0)) {
      return false;
    }
  }

  /* Channel 0 digital; channel 1 PWM; channel 2 slow PWM; channel 3 SG90 PWM.
   * At 50 Hz, 5.00..12.50% duty produces a 1.00..2.50 ms pulse. Start at
   * 7.50% (1.50 ms), then tune endpoints for the specific servo. */
  if (!mio_client_set(client, DO4_NODE_ID, 0, MIO_PROP_MODE,
                      MIO_OUTPUT_DIGITAL) ||
      !mio_client_set(client, DO4_NODE_ID, 1, MIO_PROP_MODE,
                      MIO_OUTPUT_PWM) ||
      !mio_client_set(client, DO4_NODE_ID, 1, MIO_PROP_FREQUENCY, 1000) ||
      !mio_client_set(client, DO4_NODE_ID, 1, MIO_PROP_DUTY, 1250) ||
      !mio_client_set(client, DO4_NODE_ID, 2, MIO_PROP_MODE,
                      MIO_OUTPUT_SLOW_PWM) ||
      !mio_client_set(client, DO4_NODE_ID, 2, MIO_PROP_PERIOD, 250) ||
      !mio_client_set(client, DO4_NODE_ID, 2, MIO_PROP_DUTY, 5000) ||
      !mio_client_set(client, DO4_NODE_ID, 3, MIO_PROP_MODE,
                      MIO_OUTPUT_PWM) ||
      !mio_client_set(client, DO4_NODE_ID, 3, MIO_PROP_FREQUENCY, 50) ||
      !mio_client_set(client, DO4_NODE_ID, 3, MIO_PROP_DUTY, 750)) {
    return false;
  }

  /* Keep every output disabled until the application authorizes it.
   * Example after its safety checks: mio_client_set(client, DO4_NODE_ID, 1,
   * MIO_PROP_VALUE, 1). For the SG90, enable channel 3 only after verifying
   * that the servo linkage can move safely through its travel. Update the
   * servo pulse while it is enabled without disabling it; duty 7.50% is
   * MIO_PROP_DUTY value 750. A live PWM update to 37.25% is value 3725.
   * Additional examples: pulse trigger => MIO_PROP_TRIGGER value 1;
   * slow PWM period 2.5 s => MIO_PROP_PERIOD value 25. */
  return true;
}
