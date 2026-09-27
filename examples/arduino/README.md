# Arduino IDE example

`BasicMaster/BasicMaster.ino` shows the customer-facing Arduino API. It starts
the ESP32 TWAI controller, receives discovery and status events, and shows
where an application can request an output change.

Download the packager from the public repository and create an installable
Arduino library package from the latest `main` sources:

```sh
curl -fsSL https://raw.githubusercontent.com/Weyla/MECS/main/SHARED/tools/package_arduino.py \
  -o /tmp/package_arduino.py
python3 /tmp/package_arduino.py --github-ref main /tmp
```

Install the generated `/tmp/MIOClient` folder in the Arduino IDE's libraries
directory, restart the IDE if needed, and open this example from the repository:
[BasicMaster.ino](https://github.com/Weyla/MECS/blob/main/examples/arduino/BasicMaster/BasicMaster.ino).
Select an ESP32 board whose Arduino core includes the TWAI driver. Set the CAN
TX and RX GPIOs to match the wiring; the example defaults to GPIO4 and GPIO5 at
500 kbit/s.

The adapter currently targets ESP32 Arduino cores that expose
`driver/twai.h`. Compile it with the exact Arduino-ESP32 version selected for
the product before treating that version as supported.
