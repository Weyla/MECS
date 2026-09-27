// Customer master example for the DI4 input expansion.
// Library sources and the install packager: https://github.com/Weyla/MECS
#include <MIOClient.h>

constexpr uint8_t DI4_NODE = 2;
constexpr int CAN_TX_GPIO = 4;
constexpr int CAN_RX_GPIO = 5;

// Remote channel-to-pin map on the DI4 board: 0->GPIO0, 1->GPIO1,
// 2->GPIO2, 3->GPIO3. These are expansion-board pins, not master pins.
MIOClient io;
bool configure_di4 = false;
uint16_t configured_boot_id = 0;
bool have_configured_boot_id = false;

void on_mio_event(void *, mio_client_event_t event, uint8_t node,
                  uint8_t channel, uint8_t property, uint16_t value,
                  mio_error_t error) {
  (void)node;
  (void)channel;
  (void)property;
  (void)value;
  if (event == MIO_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MIO_CLIENT_EVENT_REQUEST_TIMEOUT) {
    Serial.printf("DI4 request failed: %s\n", mio_error_name(error));
  }
}

void setup() {
  Serial.begin(115200);
  if (!io.begin(CAN_TX_GPIO, CAN_RX_GPIO, on_mio_event, nullptr, 500000)) {
    Serial.println("MIO startup failed; check CAN pins and transceiver");
  }
}

void loop() {
  io.loop();

  const mio_client_node_t *node = io.node(DI4_NODE);
  if (node != nullptr && node->status_seen &&
      (!have_configured_boot_id ||
       configured_boot_id != node->status.boot_id)) {
    configure_di4 = true; // Re-apply volatile settings after each node reboot.
  }

  if (configure_di4 && io.nodeOnline(DI4_NODE)) {
    configure_di4 = false;

    // Per-channel active polarity: 0=active-high, 1=active-low.
    io.set(DI4_NODE, 0, MIO_PROP_POLARITY, 1);
    io.set(DI4_NODE, 1, MIO_PROP_POLARITY, 0);
    io.set(DI4_NODE, 2, MIO_PROP_POLARITY, 1);
    io.set(DI4_NODE, 3, MIO_PROP_POLARITY, 1);

    // Per-channel function: MIO_FILTER_NONE, MIO_FILTER_STABLE, or
    // MIO_FILTER_PWM. Channel 2 measures PWM period and duty locally.
    io.set(DI4_NODE, 0, MIO_PROP_FILTER, MIO_FILTER_STABLE);
    io.set(DI4_NODE, 1, MIO_PROP_FILTER, MIO_FILTER_NONE);
    io.set(DI4_NODE, 2, MIO_PROP_FILTER, MIO_FILTER_PWM);
    io.set(DI4_NODE, 3, MIO_PROP_FILTER, MIO_FILTER_STABLE);

    // Stable debounce duration (1..1000 ms); used by channels 0 and 3.
    io.set(DI4_NODE, 0, MIO_PROP_FILTER_MS, 20);
    io.set(DI4_NODE, 3, MIO_PROP_FILTER_MS, 20);

    // Input bias: 0=floating, 1=pull-up, 2=pull-down.
    io.set(DI4_NODE, 0, MIO_PROP_PULL, MIO_PULL_UP);
    io.set(DI4_NODE, 1, MIO_PROP_PULL, MIO_PULL_DOWN);
    io.set(DI4_NODE, 2, MIO_PROP_PULL, MIO_PULL_UP);
    io.set(DI4_NODE, 3, MIO_PROP_PULL, MIO_PULL_UP);

    // Node-wide reporting: 0=periodic, 1=on-change, 2=both; interval 20..500 ms.
    io.set(DI4_NODE, MIO_GLOBAL_CHANNEL, MIO_PROP_REPORT, MIO_REPORT_BOTH);
    io.set(DI4_NODE, MIO_GLOBAL_CHANNEL, MIO_PROP_REPORT_MS, 100);
    configured_boot_id = node->status.boot_id;
    have_configured_boot_id = true;
  }
}
