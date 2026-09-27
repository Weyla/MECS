/* Customer ESP-IDF example using the same managed node/pin API as Arduino. */
#include "MECSClient.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

constexpr int CAN_TX_GPIO = 4;
constexpr int CAN_RX_GPIO = 5;
constexpr uint8_t DO4_NODE = 1;
constexpr uint8_t DI4_NODE = 2;

MECSClient can;
MECSNode outputNode = can.node(DO4_NODE);
MECSNode inputNode = can.node(DI4_NODE);
MECSPin servo = outputNode.pin(3); // DO4 channel 3 is GPIO3 on the prototype.
MECSPin button = inputNode.pin(0); // DI4 channel 0 is GPIO0 on the prototype.

extern "C" void app_main(void) {
  can.enableLogging();
  ESP_ERROR_CHECK(can.begin(CAN_TX_GPIO, CAN_RX_GPIO)); // 500 kbit/s by default.

  // Register requested settings once; the library confirms and restores them
  // after a node reset or CAN transport recovery.
  servo.setMode(MECS::PWM);
  servo.setPwm(50, 7.500); // Frequency in Hz; duty in percent to 0.001%.
  servo.turnOn();

  button.setMode(MECS::DEBOUNCE);
  button.setDebounce(20); // Require a stable state for 20 ms.
  button.setResistor(MECS::PullUp);
  button.setActiveLow(true); // Switch to ground is logically active.
  inputNode.setReporting(20, 500, true); // Min/max interval; include changes.

  for (;;) {
    can.loop(); // Services CAN and applies pending settings; call frequently.

    // Invalid means the node has not reported fresh configured input data.
    // This example chooses to stop the servo until the input is valid again.
    if (!button.valid()) {
      servo.turnOff();
    } else {
      servo.setPwm(50, button.value() ? 5.000 : 12.500);
      servo.turnOn();
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
