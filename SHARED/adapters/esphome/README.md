# ESPHome external component

The ESPHome adapter binds the portable MIO client to ESPHome's configured
`canbus` component. It exposes mapped DI4 inputs as binary sensors and DO4
outputs as switches. No user automation needs CAN identifiers or payloads.

Prepare a local external-component directory with the canonical protocol and
client C sources:

```sh
python3 SHARED/tools/package_esphome.py /tmp/mio-esphome
```

Add the generated folder next to the ESPHome YAML file and configure:

```yaml
esphome:
  name: mio-master
  # ESPHome compiles these portable C sources as C translation units.
  includes:
    - mio_components/mio_sources/mio.c
    - mio_components/mio_sources/mio_io.c
    - mio_components/mio_sources/mio_io_wire.c
    - mio_components/mio_sources/mio_command.c
    - mio_components/mio_sources/mio_client.c
    - mio_components/mio_sources/mio.h
    - mio_components/mio_sources/mio_io.h
    - mio_components/mio_sources/mio_command.h
    - mio_components/mio_sources/mio_client.h

external_components:
  - source:
      type: local
      path: mio_components

canbus:
  - platform: esp32_can
    id: mio_can
    can_id: 0
    tx_pin: GPIO4
    rx_pin: GPIO5
    bit_rate: 500kbps

mio_expansion:
  canbus_id: mio_can
  inputs:
    - name: "DI4 input 1"
      node: 2
      channel: 0
  outputs:
    - name: "DO4 output 1"
      node: 1
      channel: 0
```

The component increments and persists its session at boot. The ESPHome CAN bus
already initializes the controller, so do not also start the `mio_espidf`
TWAI adapter.

The Python schema and C++ component are an initial adapter and still need a
compile/configuration check against the ESPHome release selected for the
product. The C files are listed under `esphome.includes` because ESPHome copies
and compiles explicitly listed `.c` files into its generated project. See the
root `README.md` for portable-core tests.
