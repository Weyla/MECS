# ESPHome external component

The ESPHome adapter binds the portable MECS client to ESPHome's configured
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
[`examples/esphome/mecs-master.yaml`](https://github.com/Weyla/MECS/blob/main/examples/esphome/mecs-master.yaml).

```yaml
esphome:
  name: mecs-master
  # ESPHome compiles these portable C sources as C translation units.
  includes:
    - mecs_components/mecs_sources/mecs_protocol.c
    - mecs_components/mecs_sources/mecs_io.c
    - mecs_components/mecs_sources/mecs_io_wire.c
    - mecs_components/mecs_sources/mecs_client.c
    - mecs_components/mecs_sources/mecs_protocol.h
    - mecs_components/mecs_sources/mecs_io.h
    - mecs_components/mecs_sources/mecs_client.h

external_components:
  - source:
      type: git
      url: https://github.com/Weyla/MECS.git
      ref: main
      path: SHARED/adapters/esphome

canbus:
  - platform: esp32_can
    id: mecs_can
    can_id: 0
    tx_pin: GPIO4
    rx_pin: GPIO5
    bit_rate: 500kbps

mecs_expansion:
  canbus_id: mecs_can
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
already initializes the controller, so do not also start the `mecs_espidf`
TWAI adapter.

Mapped input binary sensors are invalidated to an unknown state when their node
stops reporting. ESPHome switches do not support an unknown state, so a switch
that was confirmed on is published off when its node goes offline. This only
clears the displayed stale state; the node's own heartbeat-loss behavior
returns its physical outputs to their configured inactive state.

The Python schema and C++ component are an initial adapter and still need a
compile/configuration check against the ESPHome release selected for the
product. The C files are listed under `esphome.includes` because ESPHome copies
and compiles explicitly listed `.c` files into its generated project. See the
root `README.md` for portable-core tests.

A saved master session must also pass the preference `sync()` step before it
is used. When transactions exhaust the current session, the adapter persists a
new session and calls the shared client's session transition. Failed persistence
is retried at most once per second. Session rotation stops outputs and requires
explicit enabling again. This integration still requires compilation and bench
validation against the selected ESPHome release.
