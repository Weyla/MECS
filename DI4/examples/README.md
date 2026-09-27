# DI4 input examples

These examples run on a customer-written **master** and access the fixed DI4
node over CAN. They do not run on the DI4 expansion board itself. The public
library and examples are hosted at [Weyla/MECS](https://github.com/Weyla/MECS).

## Wiring and channel map

| Function | GPIO | Notes |
|---|---:|---|
| CAN TX | 4 | ESP32-C3 TWAI TX to transceiver TXD |
| CAN RX | 5 | ESP32-C3 TWAI RX from transceiver RXD |
| DI4 channel 0 | 0 | Physical input 0 |
| DI4 channel 1 | 1 | Physical input 1 |
| DI4 channel 2 | 2 | Physical input 2 |
| DI4 channel 3 | 3 | Physical input 3 |

The CAN bus is configured for 500 kbit/s. Use a CAN transceiver, common ground,
and end-of-line termination at both ends. `node: 2` is the example node address;
change it to match the node's `CONFIG_MIO_NODE_ID` if you reconfigure firmware.

## Input options

| Option | Values | Current default |
|---|---|---|
| Active level | High / Low | Low |
| Input function | No filter / stable debounce / PWM measurement | Stable debounce |
| Debounce | 1–1000 ms | 20 ms |
| Input bias | Floating / pull-up / pull-down | Pull-up |
| Reporting | Periodic / on change / both | Both |
| Report interval | 20–500 ms | 100 ms |
| PWM measurement | Period 90–1,000,000 µs; duty 0–100% | Off until filter is set to PWM; 10 kHz is within the test range |

The filter and edge measurement run on DI4. The master receives filtered states
or completed PWM measurements; it never receives every edge. Filter settings
are per channel. Reporting mode and interval apply to the whole node.
Settings are volatile on the node; the Arduino sketch reapplies its selected
settings when the DI4 boot identifier changes.

## Examples

- [Arduino IDE](arduino/di4.ino)
- [ESP-IDF](esp-idf/configure_di4.c)
- [ESPHome](esphome/di4.yaml)

For ESPHome, fetch the required C sources from GitHub into the same folder as
the YAML, then use that folder as the ESPHome configuration directory:

```sh
curl -fsSL https://raw.githubusercontent.com/Weyla/MECS/main/SHARED/tools/package_esphome.py \
  -o /tmp/package_esphome.py
python3 /tmp/package_esphome.py --github-ref main DI4/examples/esphome
```

Copy [`examples/esphome/secrets.yaml.example`](https://github.com/Weyla/MECS/blob/main/examples/esphome/secrets.yaml.example)
to this configuration folder as `secrets.yaml` and enter the local Wi-Fi
credentials there.

The ESPHome adapter currently exposes input states as binary sensors. Its YAML
does not yet expose the DI4 filter, polarity, pull, or reporting settings; those
options are documented above and available through the portable client API in
Arduino and ESP-IDF.

The ESP-IDF file is a helper to add to a master application. Copy the Git
dependency entries from [`esp-idf/idf_component.yml`](esp-idf/idf_component.yml)
to that application's `main/idf_component.yml`, and call the helper after the
node's boot identifier changes so its volatile settings are restored.
