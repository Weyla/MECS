#include <MECSClient.h>

MECSClient can;
auto inputs = can.node(2);
auto direct = inputs.pin(0);    // DI4 GPIO0
auto button = inputs.pin(1);    // DI4 GPIO1
auto pwmSignal = inputs.pin(2); // DI4 GPIO2
auto biasedLow = inputs.pin(3); // DI4 GPIO3
uint32_t lastPrintMs = 0;

void setup() {
  Serial.begin(115200);
  can.enableLogging(Serial);
  if (!can.begin(4, 5)) return; // Master CAN TX/RX; default 500 kbit/s.

  direct.setMode(MECS::DIGITAL_INPUT);
  direct.setResistor(MECS::Floating); // Signal must be driven externally.
  direct.setActiveLow(false);

  button.setDebounce(20); // Selects stable debounce; local node timing in ms.
  button.setResistor(MECS::PullUp);
  button.setActiveLow(true); // Button connects GPIO1 to GND when pressed.

  pwmSignal.setMode(MECS::PWM_INPUT); // Edges are measured on DI4, not over CAN.
  pwmSignal.setResistor(MECS::Floating);
  pwmSignal.setActiveLow(false);

  biasedLow.setMode(MECS::DIGITAL_INPUT);
  biasedLow.setResistor(MECS::PullDown);
  biasedLow.setActiveLow(false);
  inputs.setReporting(20, 500, true); // Min gap, max gap, report-on-change.
}

void loop() {
  can.loop();
  if (button.valid() && millis() - lastPrintMs >= 1000) {
    lastPrintMs = millis();
    Serial.print("Button: "); Serial.println(button.value());
    const auto measured = pwmSignal.pwm();
    if (measured.valid) {
      Serial.print("PWM Hz: "); Serial.println(measured.frequencyHz);
      Serial.print("PWM duty %: "); Serial.println(measured.dutyPercent);
    }
  }
}
