# MECS Arduino library

`MECSClient` connects an ESP32 Arduino sketch to expansion nodes using the
on-chip TWAI controller and an external CAN transceiver. Supported bitrates
are 250, 500 (default), and 1000 kbit/s. The recovery and node/pin logic is
shared portable C/C++; the web server is not included.

## Install

Install `MECSClient.zip` using **Sketch → Include Library → Add .ZIP Library…**.
A locally built archive is available in `dist/` when the maintainer packages a
release. Published packages are available from the
[MECS releases page](https://github.com/Weyla/MECS/releases) once uploaded.
Open **File → Examples → MECSClient → ServoRecovery** for the single-output-node
servo test, or **BasicMaster** for input-controlled PWM. **DO4** and **DI4** show
all four channels and their options. ESP32-C3 native USB serial needs **USB CDC
On Boot → Enabled**.

```cpp
#include <MECSClient.h>
MECSClient can;
auto outputs = can.node(1);
auto servo = outputs.pin(3);

void setup() {
  Serial.begin(115200);
  can.enableLogging(Serial); // Optional; no callback required.
  if (!can.begin(4, 5)) return; // Master CAN TX/RX.
  servo.setPwm(50, 7.500);     // Selects PWM mode; Hz and duty percent.
  servo.turnOn();
}

void loop() {
  can.loop(); // Discovery, heartbeats, confirmations and recovery.
}
```

Settings can be registered before discovery. Repeating a setter does not send
unchanged data. `ready()` means all requested settings are acknowledged;
`online()` means fresh node status; input `valid()` distinguishes a current
reading from a disconnected node. After a node reset the library restores the
last requested settings, then the explicit enable state. Missing heartbeats
still make node outputs inactive.

Three-decimal duty and configurable reporting minimum require node firmware
0.3.1. This package provides one supported `MECSClient` API and one Arduino
TWAI transport. Remove any earlier duplicate library installation if Arduino
reports ambiguous libraries.

The [MECS API manual](https://github.com/Weyla/MECS/blob/main/SHARED/docs/MECS_API.md)
describes argument ranges, reporting, validity, failures and acceptance testing.
The facade builds against Arduino-ESP32 3.3.11 on ESP32-C3; additional boards
and core releases require their own build and hardware checks.

## Package from canonical source

From a checkout, run `python3 SHARED/tools/package_arduino.py /tmp/package`.
This creates `/tmp/package/MECSClient`; ZIP that directory for Arduino IDE.
`--github-ref <tag-or-commit>` instead retrieves sources from that GitHub ref.
The release workflow packages tags named `arduino-vMAJOR.MINOR.PATCH`.
