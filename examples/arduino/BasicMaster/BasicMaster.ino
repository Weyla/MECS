#include <MECSClient.h>

constexpr int CAN_TX_GPIO = 4;
constexpr int CAN_RX_GPIO = 5;
MECSClient can;
auto outputNode = can.node(1);
auto inputNode = can.node(2);
// These are remote channel numbers. On the prototype, channel 3 is DO4 GPIO3.
auto servo = outputNode.pin(3);
auto inputPin = inputNode.pin(0);

void setup() {
  Serial.begin(115200);
  can.enableLogging(Serial); // Optional readable diagnostics, no callback needed.
  if (!can.begin(CAN_TX_GPIO, CAN_RX_GPIO)) {
    Serial.println("MECS startup failed; check CAN configuration");
    return;
  }

  // Settings can be registered before nodes are online. loop() handles their
  // order, confirmations and reapplication after a node restarts.
  servo.setMode(MECS::PWM);
  servo.setPwm(50, 7.500); // Hz and percent, with up to three decimal places.
  servo.turnOn();

  inputPin.setMode(MECS::DEBOUNCE);
  inputPin.setDebounce(20); // Must remain stable for 20 ms before changing.
  inputPin.setResistor(MECS::PullUp);
  inputPin.setActiveLow(true); // A switch between input GPIO0 and GND is active.
  inputNode.setReporting(20, 500, true); // Min/max milliseconds; report changes.
}

void loop() {
  can.loop(); // Keep running frequently; do not block it with long delays.

  // A disconnected input is not the same as a known LOW input. This example
  // chooses to stop the servo pulses until valid input data returns.
  if (!inputPin.valid()) {
    servo.turnOff();
    return;
  }
  servo.setPwm(50, inputPin.value() ? 5.000 : 12.500);
  servo.turnOn();
  // Repeating these setters does not repeat CAN traffic when values match.
}
