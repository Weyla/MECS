/* Example managed configuration for a DI4 node, independent of CAN frames. */
#include "configure_di4.h"

bool configure_di4_inputs(MECSNode &inputs) {
  auto direct = inputs.pin(0);    // DI4 GPIO0: floating, unfiltered input.
  auto button = inputs.pin(1);    // DI4 GPIO1: active-low switch to ground.
  auto pwmSignal = inputs.pin(2); // DI4 GPIO2: measure a PWM signal locally.
  auto biasedLow = inputs.pin(3); // DI4 GPIO3: pull-down, unfiltered input.

  if (!direct.setMode(MECS::DIGITAL_INPUT) ||
      !direct.setResistor(MECS::Floating) ||
      !direct.setActiveLow(false)) {
    return false;
  }

  if (!button.setDebounce(20) || !button.setResistor(MECS::PullUp) ||
      !button.setActiveLow(true)) {
    return false;
  }

  if (!pwmSignal.setMode(MECS::PWM_INPUT) ||
      !pwmSignal.setResistor(MECS::Floating) ||
      !pwmSignal.setActiveLow(false)) {
    return false;
  }

  if (!biasedLow.setMode(MECS::DIGITAL_INPUT) ||
      !biasedLow.setResistor(MECS::PullDown) ||
      !biasedLow.setActiveLow(false)) {
    return false;
  }

  // Reporting options apply to the node, not to each channel.
  return inputs.setReporting(20, 500, true);
}
