# Arduino IDE example

`BasicMaster/BasicMaster.ino` shows the customer-facing Arduino API. It starts
the ESP32 TWAI controller and uses node/pin objects without a user callback.
`ServoRecovery/ServoRecovery.ino` tests a servo on node 1, channel 3, including
automatic reconfiguration when the node resets.

Install the locally built `dist/MECSClient.zip`, or obtain the `MECSClient` library from a published
[Arduino library release](https://github.com/Weyla/MECS/releases/latest):

1. Download `MECSClient.zip` from the release assets.
2. In Arduino IDE, choose **Sketch → Include Library → Add .ZIP Library…** and
   select the ZIP.
3. Open **File → Examples → MECSClient → BasicMaster**.

If the release has no `MECSClient.zip` asset yet, its first Arduino library
package has not been published. The example source is also available here:
[BasicMaster.ino](https://github.com/Weyla/MECS/blob/main/examples/arduino/BasicMaster/BasicMaster.ino).
Select an ESP32 board whose Arduino core includes the TWAI driver. Set the CAN
TX and RX GPIOs to match the wiring; the example defaults to GPIO4 and GPIO5 at
500 kbit/s.

The adapter currently targets ESP32 Arduino cores that expose
`driver/twai.h`. Compile it with the exact Arduino-ESP32 version selected for
the product before treating that version as supported.

Read the [MECS API and recovery manual](../../SHARED/docs/MECS_API.md) for
confirmation, input validity, supported ranges and node firmware requirements.
