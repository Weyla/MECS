// Customer master example for the DO4 output expansion.
// Library sources and the install packager: https://github.com/Weyla/MECS
#include <MIOClient.h>

constexpr uint8_t DO4_NODE = 1;
constexpr int CAN_TX_GPIO = 4;
constexpr int CAN_RX_GPIO = 5;

// Remote channel-to-pin map on the DO4 board: 0->GPIO0, 1->GPIO1,
// 2->GPIO2, 3->GPIO3. These are expansion-board pins, not master pins.
MIOClient io;
bool configure_do4 = false;
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
    Serial.printf("DO4 request failed: %s\n", mio_error_name(error));
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

  const mio_client_node_t *node = io.node(DO4_NODE);
  if (node != nullptr && node->status_seen &&
      (!have_configured_boot_id ||
       configured_boot_id != node->status.boot_id)) {
    configure_do4 = true; // Re-apply settings after each node reboot.
  }

  if (configure_do4 && io.nodeOnline(DO4_NODE)) {
    configure_do4 = false;

    // Per-channel polarity: 0=active-high, 1=active-low.
    for (uint8_t channel = 0; channel < 4; ++channel) {
      io.set(DO4_NODE, channel, MIO_PROP_POLARITY, 0);
    }

    // Channel 0: digital output; channel 1: 1 kHz PWM at 12.50%;
    // channel 2: slow PWM with a 25.0-second period; channel 3: SG90 servo.
    io.set(DO4_NODE, 0, MIO_PROP_MODE, MIO_OUTPUT_DIGITAL);
    io.set(DO4_NODE, 1, MIO_PROP_MODE, MIO_OUTPUT_PWM);
    io.set(DO4_NODE, 1, MIO_PROP_FREQUENCY, 1000);
    io.setPwmDuty(DO4_NODE, 1, 12.50f);
    io.set(DO4_NODE, 2, MIO_PROP_MODE, MIO_OUTPUT_SLOW_PWM);
    io.set(DO4_NODE, 2, MIO_PROP_PERIOD, 250); // 25.0 seconds
    io.set(DO4_NODE, 2, MIO_PROP_DUTY, 5000);  // 50.00 percent
    io.set(DO4_NODE, 3, MIO_PROP_MODE, MIO_OUTPUT_PWM);
    io.set(DO4_NODE, 3, MIO_PROP_FREQUENCY, 50);
    io.setPwmDuty(DO4_NODE, 3, 7.50f); // 1.50 ms pulse: nominal midpoint

    // After application safety checks, enable channel 1 explicitly with:
    // io.setOutput(DO4_NODE, 1, true);
    // Update its duty while enabled with:
    // io.setPwmDuty(DO4_NODE, 1, 37.25f);
    // After checking the linkage's safe travel, enable the servo with:
    // io.setOutput(DO4_NODE, 3, true);
    // Move it while enabled with io.setPwmDuty(DO4_NODE, 3, 5.00f) through
    // io.setPwmDuty(DO4_NODE, 3, 12.50f); tune limits for your servo/linkage.
    configured_boot_id = node->status.boot_id;
    have_configured_boot_id = true;
  }
}
