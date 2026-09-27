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
change it to match the node's `CONFIG_MIO_NODE_ID` if you reconfigure firmware.

## Output options

| Option | Values | Notes |
|---|---|---|
| Active level | High / Low | Low is available as a node build-time default; runtime polarity is per channel |
| Mode | Digital / PWM / timed pulse / slow PWM | Modes are processed on the node |
| PWM frequency | 10–10,000 Hz | Applies to frequency PWM mode |
| PWM active duty | 0.00–100.00% | Percent supports two decimal places; can change while output is enabled |
| Timed pulse | 10–60,000 ms | Start with the trigger property; node ends it locally |
| Slow PWM period | 0.1–3,600.0 s | Encoded in tenths of a second; local generation, independent of CAN timing |
| Output gate | Off / On | Each channel is controlled independently |

All outputs start inactive. Master heartbeat loss or a new master session
returns them inactive; they do not turn back on until explicitly enabled.
The Arduino example reapplies mode settings after a node reboot but deliberately
leaves output gates off until application logic explicitly enables a channel.

## Examples

- [Arduino IDE](arduino/do4.ino)
- [ESP-IDF](esp-idf/configure_do4.c)
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

The ESP-IDF file is a helper to add to a master application. Copy the Git
dependency entries from [`esp-idf/idf_component.yml`](esp-idf/idf_component.yml)
to that application's `main/idf_component.yml`, and call the helper after the
node's boot identifier changes so its volatile settings are restored.
