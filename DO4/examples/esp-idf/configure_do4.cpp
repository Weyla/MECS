/* Example managed configuration for a DO4 node, independent of CAN frames. */
#include "configure_do4.h"

bool configure_do4_outputs(MECSNode &outputs) {
  auto digital = outputs.pin(0); // DO4 GPIO0: digital output.
  auto fastPwm = outputs.pin(1); // DO4 GPIO1: 1 kHz PWM.
  auto slowPwm = outputs.pin(2); // DO4 GPIO2: 25-second PWM period.
  auto servo = outputs.pin(3);   // DO4 GPIO3: SG90-style servo signal.

  // Settings are recorded locally and applied with confirmation by can.loop().
  // No channel enables until the application explicitly calls turnOn().
  if (!digital.setMode(MECS::DIGITAL_OUTPUT) ||
      !digital.setActiveLow(false) || !digital.turnOff()) {
    return false;
  }

  if (!fastPwm.setPwm(1000, 12.501) || !fastPwm.setActiveLow(false) ||
      !fastPwm.turnOff()) {
    return false;
  }

  if (!slowPwm.setSlowPwm(25.0, 50.000) ||
      !slowPwm.setActiveLow(false) || !slowPwm.turnOff()) {
    return false;
  }

  // A 7.5% pulse at 50 Hz is nominally 1.5 ms. Tune for the specific servo.
  return servo.setPwm(50, 7.500) && servo.setActiveLow(false) &&
         servo.turnOff();
}
