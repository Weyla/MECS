# Arduino IDE adapter

`MIOClient` is the ESP32 Arduino facade over the same portable C client used by
ESP-IDF. It uses the ESP32's on-chip TWAI controller and needs the external CAN
transceiver. The initial adapter supports 250, 500 and 1000 kbit/s.

Build an installable Arduino library folder from the canonical shared source:

```sh
python3 SHARED/tools/package_arduino.py /tmp
```

The script creates `/tmp/MIOClient`; zip that folder or copy it to the Arduino
libraries directory. The library includes the canonical `mio_core` and
`mio_client` C sources so there is no second implementation to maintain.

Example:

```cpp
#include <MIOClient.h>
#include <Preferences.h>

MIOClient io;

void setup() {
  Serial.begin(115200);
  // CAN TX/RX are project wiring choices; 500 kbit/s matches the test bus.
  if (!io.begin(4, 5, onMioEvent)) {
    Serial.println("MIO CAN startup failed");
  }
}

void loop() {
  io.loop(); // Receives frames and services heartbeat/discovery/timeouts.
}
```

The Arduino facade persists a new master session at startup and rotates it
when its transaction counter is exhausted. The event callback has the
`mio_client_event_fn` signature. Call
`setOutput(node, channel, enabled)` to change an output gate, then wait for the
confirmed event. `setPwmDuty()` accepts percent with decimals, such as `12.5f`.
Keep the callback short; it runs inside `io.loop()`.

This adapter targets ESP32 Arduino cores exposing Espressif's `driver/twai.h`
legacy driver API. Compile it against the Arduino-ESP32 version selected for
the product before claiming support for additional core releases.
