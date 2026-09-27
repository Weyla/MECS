# ESPHome external component

The ESPHome adapter binds the portable MIO client to ESPHome's configured
`canbus` component. It exposes mapped DI4 inputs as binary sensors and DO4
outputs as switches. No user automation needs CAN identifiers or payloads.

The ESPHome adapter is fetched from
[Weyla/MECS](https://github.com/Weyla/MECS) by the example YAML. Download the
portable C sources needed by ESPHome's build from the GitHub `main` branch:

```sh
curl -fsSL https://raw.githubusercontent.com/Weyla/MECS/main/SHARED/tools/package_esphome.py \
  -o /tmp/package_esphome.py
python3 /tmp/package_esphome.py --github-ref main .
```

Keep the generated sources beside the YAML file. A complete master
configuration is maintained at
[`examples/esphome/mio-master.yaml`](https://github.com/Weyla/MECS/blob/main/examples/esphome/mio-master.yaml).

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
      type: git
      url: https://github.com/Weyla/MECS.git
      ref: main
      path: SHARED/adapters/esphome

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
