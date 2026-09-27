#include <MECSClient.h>

MECSClient can;
auto outputs = can.node(1);
auto servo = outputs.pin(3); // Channel 3 is GPIO3 on the DO4 prototype.
uint32_t lastMoveMs = 0;
bool highPosition = false;

void setup() {
  Serial.begin(115200);
  can.enableLogging(Serial);
  if (!can.begin(4, 5)) { // Master CAN TX=GPIO4, RX=GPIO5, 500 kbit/s.
    Serial.println("MECS could not start");
    return;
  }
  servo.setMode(MECS::PWM);
  servo.setPwm(50, 7.500); // 1.5 ms pulse, nominal SG90 midpoint.
  servo.turnOn();
}

void loop() {
  can.loop();
  if (servo.ready() && millis() - lastMoveMs >= 1000) {
    lastMoveMs = millis();
    highPosition = !highPosition;
    servo.setPwm(50, highPosition ? 12.500 : 5.500);
  }
  // Reset/reconnect DO4 while running. The library restores the last requested
  // position and enable state; this sketch never needs to inspect a boot ID.
}
