# DO4 output examples

These examples run on a customer-written **master** and control the fixed DO4
node over CAN. They do not run on the DO4 expansion board itself. The public
library and examples are hosted at [Weyla/MECS](https://github.com/Weyla/MECS).

## Wiring and channel map

| Function | GPIO | Notes |
|---|---:|---|
| CAN TX | 4 | ESP32-C3 TWAI TX to transceiver TXD |
| CAN RX | 5 | ESP32-C3 TWAI RX from transceiver RXD |
| DO4 channel 0 | 0 | Physical output 0 |
| DO4 channel 1 | 1 | Physical output 1 |
| DO4 channel 2 | 2 | Physical output 2 |
| DO4 channel 3 | 3 | Physical output 3 |

The CAN bus is configured for 500 kbit/s. Use a CAN transceiver, common ground,
and end-of-line termination at both ends. `node: 1` is the example address;
change it to match the node's `CONFIG_MECS_NODE_ID` if you reconfigure firmware.

## Output options

| Option | Values | Notes |
|---|---|---|
| Active level | High / Low | Low is available as a node build-time default; runtime polarity is per channel |
| Mode | Digital / PWM / timed pulse / slow PWM | Modes are processed on the node |
| PWM frequency | 10–10,000 Hz | Applies to frequency PWM mode |
| PWM active duty | 0.000–100.000% | Managed API supports three decimal places; legacy duty supports two; can change while output is enabled |
| Timed pulse | 10–60,000 ms | Start with the trigger property; node ends it locally |
| Slow PWM period | 0.1–3,600.0 s | Encoded in tenths of a second; local generation, independent of CAN timing |
| Output gate | Off / On | Each channel is controlled independently |

All outputs start inactive. Master heartbeat loss or a new master session
returns them inactive; they do not turn back on until explicitly enabled.
The Arduino library reapplies requested configuration after a node reboot.
This example explicitly enables channel 3 for the servo; channels 0–2 remain
off. Use `turnOn()`/`turnOff()` for independent runtime control. The last
requested enable state is restored after reconnect by the running master.

## Examples

- [Arduino IDE](arduino/do4.ino)
- [ESP-IDF helper](esp-idf/configure_do4.cpp)
- [ESPHome](esphome/do4.yaml)

For ESPHome, fetch the required C sources from GitHub into the same folder as
the YAML, then use that folder as the ESPHome configuration directory:

```sh
curl -fsSL https://raw.githubusercontent.com/Weyla/MECS/main/SHARED/tools/package_esphome.py \
  -o /tmp/package_esphome.py
python3 /tmp/package_esphome.py --github-ref main DO4/examples/esphome
```

Copy [`examples/esphome/secrets.yaml.example`](https://github.com/Weyla/MECS/blob/main/examples/esphome/secrets.yaml.example)
to this configuration folder as `secrets.yaml` and enter the local Wi-Fi
credentials there.

The ESPHome adapter currently exposes each output as an on/off switch. Its YAML
does not yet expose runtime PWM, pulse, period, or polarity controls; those
options are available through the portable client API in Arduino and ESP-IDF.

The ESP-IDF files are helpers for a customer-written master application. Copy
`configure_do4.cpp` and `configure_do4.h` into the application's `main`
component, add the `.cpp` to that component's `SRCS`, and add
`mecs_espidf_client` to its `REQUIRES`. Copy the component dependencies from
[`esp-idf/idf_component.yml`](esp-idf/idf_component.yml) into the application
manifest. After `can.begin(4, 5)`, create `auto outputs = can.node(1);` and call
`configure_do4_outputs(outputs)` once. A `true` return means the settings were
accepted by the local API; `can.loop()` sends them and processes acknowledgments.
All outputs remain off until the application explicitly calls a channel's
`turnOn()`. The managed client re-applies settings after a DO4 reset; no boot-ID
handling is needed. Keep calling `can.loop()` from the owner task. For example:

```cpp
#include "MECSClient.h"
#include "configure_do4.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

MECSClient can;
MECSNode outputs = can.node(1);
MECSPin servo = outputs.pin(3);

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(can.begin(4, 5));
  ESP_ERROR_CHECK(configure_do4_outputs(outputs) ? ESP_OK : ESP_FAIL);
  servo.turnOn();
  // Later, update live without turning the servo output off:
  // servo.setDuty(12.500);
  for (;;) {
    can.loop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
```

The application's `main/CMakeLists.txt` must compile the helper and link the
client component, for example:

```cmake
idf_component_register(SRCS "main.cpp" "configure_do4.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES mecs_espidf_client freertos)
```

See the [MECS API manual](../../SHARED/docs/MECS_API.md) for managed settings,
confirmation, three-decimal duty and reset recovery.
