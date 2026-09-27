#include <MECSClient.h>

MECSClient can;
auto outputs = can.node(1);
// All four remote channels and their prototype GPIO mapping:
auto digital = outputs.pin(0); // DO4 GPIO0
// PWM generation runs on the node, independently of CAN update timing.
auto fastPwm = outputs.pin(1); // DO4 GPIO1
auto slowPwm = outputs.pin(2); // DO4 GPIO2
auto servo = outputs.pin(3);   // DO4 GPIO3

void setup() {
  Serial.begin(115200);
  can.enableLogging(Serial);
  if (!can.begin(4, 5)) return; // Master CAN pins, not the remote I/O pins.

  digital.setMode(MECS::DIGITAL_OUTPUT);
  digital.setActiveLow(false); // true would make logical ON drive LOW.
  digital.turnOff();

  fastPwm.setPwm(1000, 12.501); // Hz, active duty percent (0.000..100.000).
  fastPwm.setActiveLow(false);
  fastPwm.turnOff();

  slowPwm.setSlowPwm(25.0, 50.000); // Period in seconds; active duty in percent.
  slowPwm.setActiveLow(false);
  slowPwm.turnOff();

  servo.setPwm(50, 7.500); // SG90 nominal midpoint, 1.50 ms active pulse.
  servo.setActiveLow(false);
  servo.turnOn(); // Explicit enable; only this channel starts producing pulses.
}

void loop() {
  can.loop();
  // Live edits need no turnOff(): servo.setPwm(50, 12.500);
  // digital.turnOn(); fastPwm.turnOn(); slowPwm.turnOn();
  // Use servo.ready() for applied settings; outputs.online() for connection.
}
