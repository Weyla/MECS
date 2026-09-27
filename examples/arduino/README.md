# Arduino IDE example

`BasicMaster/BasicMaster.ino` shows the customer-facing Arduino API. It starts
the ESP32 TWAI controller, receives discovery and status events, and shows
where an application can request an output change.

From the repository root, create an installable Arduino library package:

```sh
python3 SHARED/tools/package_arduino.py /tmp
```

Install the generated `/tmp/MIOClient` folder in the Arduino IDE's libraries
directory, restart the IDE if needed, and open
`examples/arduino/BasicMaster/BasicMaster.ino`. Select an ESP32 board whose
Arduino core includes the TWAI driver. Set the CAN TX and RX GPIOs to match the
wiring; the example defaults to GPIO4 and GPIO5 at 500 kbit/s.

The adapter currently targets ESP32 Arduino cores that expose
`driver/twai.h`. Compile it with the exact Arduino-ESP32 version selected for
the product before treating that version as supported.
