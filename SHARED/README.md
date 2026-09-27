# Modular I/O firmware tree

This workspace has three independently buildable ESP-IDF applications. Board
identity, GPIO mapping and the application receive loop stay in the board's own
`main.c`; shared components provide reusable services called by that code.

| Folder | Purpose |
|---|---|
| `MASTER/` | Master ESP32-C3 project, Wi-Fi dashboard, serial commands and CAN coordination |
| `DO4/` | Four-channel digital/PWM output node; address 1 by default |
| `DI4/` | Four-channel digital/PWM-measurement input node; address 2 by default |
| `SHARED/components/mio_core` | Portable identity, discovery, I/O model, wire codec and command parser |
| `SHARED/components/mio_client` | Portable master client: discovery, cached state, heartbeats and confirmed requests |
| `SHARED/components/mio_espidf` | ESP-IDF TWAI/CAN frame transport |

## Where to start reading

1. Open the board's `main/main.c`. The identity, pin map, startup calls and main
   loop are all visible there.
2. Follow each `mio_...()` call into `SHARED/components/mio_core` to read its
   portable protocol behavior.
3. For customer-written master applications, read
   [docs/CLIENT_API.md](docs/CLIENT_API.md), then follow `mio_client_...()` into
   `mio_client` for discovery and channel requests.
4. Follow `mio_can_...()` into `mio_espidf` for ESP-IDF CAN transport.

The web UI and web server belong to the `MASTER` test application only. They
are not dependencies of `mio_client`.

The framework-independent client API is described in
[docs/CLIENT_API.md](docs/CLIENT_API.md). Customer examples are grouped at the
repository root in [`../examples/`](../examples/): ESP-IDF, Arduino IDE, and
ESPHome. The Arduino IDE package is assembled from canonical shared sources by
`tools/package_arduino.py`; the initial ESPHome external component is assembled
by `tools/package_esphome.py`.

For example, each node creates its own `mio_identity_t`. Its receive loop calls
`mio_is_discovery_request()` and then `mio_announce(&protocol,
MIO_ANNOUNCE_REQUEST)`. The shared announce function formats the identity that
this board supplied during `mio_init()`. This keeps the response path visible
in the board application and the CAN frame layout reusable.

## Build

Use ESP-IDF 6.1, target ESP32-C3, and build from this directory:

```sh
idf.py -C MASTER build
idf.py -C DO4 build
idf.py -C DI4 build
```

Each project points ESP-IDF to `../SHARED/components`. Build configuration is
stored per project, so changing one board does not silently change another.
CAN and channel pin assignments are declared in each board project. The current
test wiring uses CAN TX GPIO4, RX GPIO5 at 500 kbit/s; DO4 and DI4 channels use
GPIO0, 1, 2 and 3.

## Protocol and behavior

See [docs/PROTOCOL.md](docs/PROTOCOL.md), [docs/COMMISSIONING.md](docs/COMMISSIONING.md),
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) and [docs/VERIFICATION.md](docs/VERIFICATION.md).
