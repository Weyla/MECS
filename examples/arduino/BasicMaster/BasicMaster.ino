#include <MIOClient.h>

// These pins match the current ESP32-C3 master test wiring.
constexpr int CAN_TX_GPIO = 4;
constexpr int CAN_RX_GPIO = 5;
MIOClient io;

void onMioEvent(void *, mio_client_event_t event, uint8_t node,
                uint8_t channel, uint8_t property, uint16_t value,
                mio_error_t error) {
  if (event == MIO_CLIENT_EVENT_NODE_ANNOUNCED) {
    Serial.printf("Node %u announced, board type %u\n", node, value);
  } else if (event == MIO_CLIENT_EVENT_REQUEST_CONFIRMED) {
    Serial.printf("Node %u channel %u property %u confirmed as %u\n",
                  node, channel, property, value);
  } else if (event == MIO_CLIENT_EVENT_REQUEST_REJECTED ||
             event == MIO_CLIENT_EVENT_REQUEST_TIMEOUT) {
    Serial.printf("Node %u request failed, error %u\n", node, error);
  }
}

void setup() {
  Serial.begin(115200);
  if (!io.begin(CAN_TX_GPIO, CAN_RX_GPIO, onMioEvent)) {
    Serial.println("MIO startup failed; check CAN pins, transceiver and bitrate");
  }
}

void loop() {
  io.loop();

  // Example output call, normally triggered by application logic:
  // if (io.nodeOnline(1)) io.setOutput(1, 0, true);
  // Wait for MIO_CLIENT_EVENT_REQUEST_CONFIRMED before treating it as applied.
}
