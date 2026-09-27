# Modular I/O

Firmware and libraries for a CAN-connected modular I/O system. The expansion
nodes run fixed firmware; a customer-written master application uses the
portable MIO client library to discover nodes, configure channels, read cached
values and change outputs without creating CAN frames.

## Repository layout

| Folder | Purpose |
|---|---|
| `MASTER/` | ESP-IDF master test application, including its demonstration web UI |
| `DI4/` | Fixed four-input node firmware |
| `DO4/` | Fixed four-output node firmware |
| `SHARED/components/mio_core/` | Portable protocol and I/O model used by master and nodes |
| `SHARED/components/mio_client/` | Public, framework-independent master client API |
| `SHARED/components/mio_espidf/` | ESP-IDF TWAI frame transport |
| `SHARED/adapters/arduino/` | Arduino-ESP32 facade and library packager |
| `SHARED/adapters/esphome/` | ESPHome external-component adapter and packager |
| `examples/esp-idf/` | ESP-IDF customer master example |
| `examples/arduino/` | Arduino IDE customer master example |
| `examples/esphome/` | ESPHome YAML example |
| `SHARED/docs/` | Protocol, commissioning, development and client API manuals |
| `SHARED/tests/` | Host-side tests for portable client behavior |

The whole repository is intended to be cloned as one portable project. Build
the IDF apps from the repository root:

```sh
idf.py -C MASTER build
idf.py -C DI4 build
idf.py -C DO4 build
```

Run the portable client tests with CMake and CTest:

```sh
cmake -S SHARED/tests -B /tmp/mio-tests
cmake --build /tmp/mio-tests
ctest --test-dir /tmp/mio-tests --output-on-failure
```

The master web server is only part of the test application. It is not included
in the public MIO client library and is not needed by customer firmware.

Framework examples are collected under the root `examples/` folder, with
module-specific examples under `DI4/examples/` and `DO4/examples/`. ESP-IDF
examples fetch components through GitHub Component Manager. Arduino and
ESPHome packagers can download canonical sources directly from GitHub with
`--github-ref main`. Arduino users can install the release ZIP from the IDE's
**Sketch → Include Library → Add .ZIP Library…** menu; see
[`examples/arduino/README.md`](examples/arduino/README.md). The ZIP is created
for versioned `arduino-vMAJOR.MINOR.PATCH` tags. The ESPHome adapter still
needs a complete firmware build check against a selected ESPHome release before
it should be considered a released integration.

Module examples: [DI4 inputs](https://github.com/Weyla/MECS/tree/main/DI4/examples)
and [DO4 outputs](https://github.com/Weyla/MECS/tree/main/DO4/examples).

To build the ESP-IDF client example, use
`idf.py -C examples/esp-idf/master_client build`.

See [`SHARED/README.md`](SHARED/README.md) for the project map and
[`SHARED/docs/CLIENT_API.md`](SHARED/docs/CLIENT_API.md) for the customer API.
