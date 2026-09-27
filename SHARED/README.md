# Modular I/O firmware tree

This workspace has three independently buildable ESP-IDF applications. Board
identity, GPIO mapping and the application receive loop stay in the board's own
`main.c`; shared components provide reusable services called by that code.

| Folder | Purpose |
|---|---|
| `MASTER/` | Master ESP32-C3 project, Wi-Fi dashboard, serial commands and CAN coordination |
| `DO4/` | Four-channel digital/PWM output node; address 1 by default |
| `DI4/` | Four-channel digital/PWM-measurement input node; address 2 by default |
| `SHARED/components/mecs_core` | Portable identity, discovery, I/O model and wire codec |
| `SHARED/components/mecs_command` | Test-master serial/web command grammar |
| `SHARED/components/mecs_client` | Portable master client: discovery, cached state, heartbeats and confirmed requests |
| `SHARED/components/mecs_espidf` | ESP-IDF TWAI/CAN transport shared by master and nodes |
| `SHARED/components/mecs_espidf_client` | ESP-IDF master adapter with the managed `MECSClient` API |

## Where to start reading

1. Open the board's `main/main.c`. The identity, pin map, startup calls and main
   loop are all visible there.
2. Follow each `mecs_...()` call into `SHARED/components/mecs_core` to read its
   portable protocol behavior.
3. For customer-written master applications, read
   [docs/MECS_CLIENT_API.md](docs/MECS_CLIENT_API.md), then follow `mecs_client_...()` into
   `mecs_client` for discovery and channel requests.
4. ESP-IDF master applications use `mecs_espidf_client` and `MECSClient.h` for
   the same node/pin API as Arduino. `mecs_espidf` exposes `mecs_can_...()` for
   lower-level projects and the fixed node firmware.

The command parser, web UI and web server belong to the `MASTER` test
application only. They are not dependencies of node firmware or `mecs_client`.

The framework-independent client API is described in
[docs/MECS_CLIENT_API.md](docs/MECS_CLIENT_API.md). Customer examples are grouped at the
repository root in [`../examples/`](../examples/): ESP-IDF, Arduino IDE, and
ESPHome. The Arduino IDE package is assembled from canonical shared sources by
`tools/package_arduino.py`; the initial ESPHome external component is assembled
by `tools/package_esphome.py`.

For example, each node creates its own `mecs_identity_t`. Its receive loop calls
`mecs_is_discovery_request()` and then `mecs_announce(&protocol,
MECS_ANNOUNCE_REQUEST)`. The shared announce function formats the identity that
this board supplied during `mecs_init()`. This keeps the response path visible
in the board application and the CAN frame layout reusable.

## Build

Use ESP-IDF 5.5 or newer, target ESP32-C3, and build from the repository root:

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
