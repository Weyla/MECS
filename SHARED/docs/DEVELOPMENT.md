# Developer guide

| Document control | Value |
|---|---|
| Document ID | MIO-DEV-001 |
| Revision / date | 3.1 / 2026-09-27 |
| Software / wire | 0.3.0 / 3 |
| Audience | Firmware developers familiar with Arduino-style sketches |

## 1. Project map

The board project owns its identity, wiring, startup and main loop. Shared
components provide functions the board calls. Begin in the folder for the board
you are changing.

| Path | Responsibility |
|---|---|
| `MASTER/main/main.c` | Master startup and owner loop |
| `MASTER/main/master_state.c` | Discovery registry and CAN transaction engine |
| `MASTER/main/master_commands.c` | Serial and web command parsing and queueing |
| `MASTER/main/master_console.c` | USB serial input |
| `MASTER/main/master_json.c` | Dashboard state serialization |
| `MASTER/main/board_config.h` | Master CAN TX/RX GPIO assignments |
| `MASTER/main/network.c`, `web.c`, `index.html` | Wi-Fi, HTTP API and dashboard |
| `DO4/main/main.c` | Output identity, pin map, boot setup and output service loop |
| `DO4/main/output_hardware.c` | DO4 GPIO and LEDC output implementation |
| `DI4/main/main.c` | Input identity, pin map, boot setup and measurement loop |
| `DI4/main/input_hardware.c` | DI4 GPIO setup and edge timestamping |
| `DI4/main/pwm_measurement.c` | DI4-only edge-to-period/duty calculation |
| `SHARED/components/mio_core` | Portable discovery, typed I/O model, measurement wire codec and command parser |
| `SHARED/components/mio_client` | Portable master client API and cached node state |
| `SHARED/components/mio_espidf` | ESP-IDF CAN/TWAI driver adapter |
| `SHARED/docs` | Product manuals and protocol reference |

Each board has a separate ESP-IDF project and separate `sdkconfig`. Each project
points to `../SHARED/components` from its top-level CMake file. This keeps the
build targets independent while sharing the same libraries.

## 2. Read a node from startup

Open `DI4/main/main.c` or `DO4/main/main.c`. Near the top, each file lists the
node address, board type, firmware version, CAN pins and four local GPIO pins.
The `mio_identity_t` in `app_main()` repeats the identity fields that go onto
the bus. This deliberate repetition makes it easy to compare the board label,
local wiring and announced values.

Startup then follows a visible sequence:

1. `mio_io_node_init()` creates the portable channel settings and binds
   the local input/output function for applying a property.
2. DI4 installs GPIO edge handlers; DO4 prepares its output pins when applying each channel.
3. Each channel is initialized to its safe inactive/input state.
4. `mio_init()` stores this board's identity and the shared CAN send callback.
5. `mio_can_start()` starts the CAN controller.
6. `mio_announce(..., MIO_ANNOUNCE_BOOT)` sends this board's identity.

The node main loop runs every nominal millisecond. It samples its pins, advances
local filtering/pulse/slow-PWM state, polls CAN recovery and processes received
frames. It sends replies before announcements and status. The DI4 loop also
sends the latest captured period and duty after status.

## 3. Trace the discovery request

A master calls `mio_request_discovery()`; the shared function constructs a
standard CAN data frame with identifier `0x080` and the protocol revision.
The CAN driver copies the frame into its transmit slot.

On DI4 or DO4, the receive loop calls `mio_is_discovery_request()`. That shared
helper checks frame type, identifier, payload size and revision. The board
application then marks an announcement for sending and later calls
`mio_announce(&protocol, MIO_ANNOUNCE_REQUEST)`. The `protocol` instance
contains the identity that this board passed to `mio_init()`; the shared
announce function writes those identity fields into its node-specific CAN frame.
This makes the request-to-response path visible in board code while keeping
byte order and frame layout in one library.

The master passes received announcements to `mio_receive()`. The core validates
the frame and calls the master's discovery callback with typed identity fields.
The master copies that identity into its node registry.

A frame's appearance on CAN confirms a transmission was accepted or acknowledged
at link level; it does not confirm that remote application code acted on it.

## 4. How nodes filter CAN requests

The ESP32-C3 CAN adapter currently accepts standard CAN data frames into its
receive queue. This keeps message-ID policy out of the low-level driver. Each
node's receive loop first recognizes the two messages addressed to every node:
the master heartbeat and the discovery broadcast.

A property command is different. Its identifier is 0x200 plus the destination
node address. For example, node 1 receives commands on 0x201 and node 2 on
0x202. The board passes its own NODE_ADDRESS to mio_io_decode_request(). That
shared decoder checks the exact identifier, standard-frame type, DLC, and
nonzero transaction/session fields before decoding the payload. A command for
another node returns false; the node does not pass it to mio_io_node_request()
or change any channel state.

The CAN controller therefore sees the bus frames, while software decides which
ones belong to this node. Discovery and heartbeat are broadcasts by design.
The master applies a similar check to reply/status/measurement identifier ranges,
then matches replies to the outstanding node, transaction, session and property.

## 5. Follow an I/O setting

A dashboard or serial command is parsed by `mio_command_parse()` into a typed
property. The master queues the request and sends one transaction at a time.
On the node, `mio_io_decode_request()` validates the frame, then
`mio_io_node_request()` checks session, channel, property and value. It stages
the new configuration, invokes the callback supplied by this board, and commits
the value only if the hardware accepts it. `mio_io_encode_reply()` returns the
confirmed value to the master.

Inputs filter locally and only report processed values. DI4's input_hardware.c
timestamps GPIO edges and its pwm_measurement.c calculates period and active duty.
DO4 generates PWM or slow PWM in output_hardware.c. The shared measurement
encoder/decoder carries the completed result over CAN, not individual edges.

## 6. Shared library boundaries

`mio_core` is portable C. It knows fixed message layouts and I/O rules, but does
not include ESP-IDF or call hardware directly. Node applications pass a send
callback to `mio_init()` and a hardware-apply callback to the I/O model.

`mio_client` is the public master-side C API. It owns discovery, the master
heartbeat, cached node state, and one acknowledged property request at a time.
Read [CLIENT_API.md](CLIENT_API.md) before adding framework-specific code. The
master web server is kept in `MASTER/` as a test application and is not a
dependency of the public library.

`mio_espidf` converts the portable `mio_frame_t` into ESP-IDF TWAI frames. It
copies received data from its interrupt callback into a bounded queue. Board
loops call `mio_can_receive()` and process frames outside the interrupt.

The board-local hardware callback then applies the staged setting. DO4 uses
LEDC for frequency-based PWM and GPIO for digital or slow PWM transitions.

When adding a property, update the enum, property metadata, validation/read/apply
logic, wire behavior if needed, the manuals and relevant checks together. Keep
board-specific identity, pin choices and startup decisions in that board's
folder. Put a helper in `SHARED/components` only when it is reusable and its
inputs/outputs are clear to both board applications.

## 7. Build and extend

Build each project from the workspace root:

```sh
idf.py -C MASTER build
idf.py -C DO4 build
idf.py -C DI4 build
```

The board project directories are independent. A developer can build or flash
one without regenerating another board's configuration. Do not copy an
application's `sdkconfig` to a different board project; the defaults and node
identity differ.

An STM32 port can reuse `mio_core` and provide a new CAN adapter and hardware
callbacks. An Arduino-ESP32 TWAI facade and library-packaging script are in
`SHARED/adapters/arduino` and `SHARED/tools/package_arduino.py`; test the
generated library with the selected Arduino-ESP32 core before claiming a
supported release. Analog input, more module profiles, hardware-assisted pulse
capture and persistent channel settings remain future work. The ESPHome adapter
source and package script are present, but still need a build/configuration
check against the selected ESPHome release before it is treated as a supported
integration.
