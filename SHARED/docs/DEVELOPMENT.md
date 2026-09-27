# Developer guide

| Document control | Value |
|---|---|
| Document ID | MECS-DEV-001 |
| Revision / date | 3.2 / 2026-09-27 |
| Firmware / wire | 0.3.1 / 3 |
| Audience | Developers extending the board firmware and master client |

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
| `SHARED/components/mecs_core` | Portable discovery, typed I/O model and measurement wire codec |
| `SHARED/components/mecs_command` | Test-master serial/web command grammar |
| `SHARED/components/mecs_client` | Portable master client API and cached node state |
| `SHARED/components/mecs_espidf` | ESP-IDF CAN/TWAI driver, shared by master and node firmware |
| `SHARED/components/mecs_espidf_client` | Master-only `MECSClient` adapter with NVS sessions and owner loop |
| `SHARED/docs` | Product manuals and protocol reference |

Each board has a separate ESP-IDF project and separate `sdkconfig`. Each project
points to `../SHARED/components` from its top-level CMake file. This keeps the
build targets independent while sharing the same libraries.

The firmware uses ESP-IDF 5.5 or newer because its CAN transport uses the
on-chip TWAI node API. GitHub Actions builds all three fixed firmware projects
with the minimum supported release.

## 2. Read a node from startup

Open `DI4/main/main.c` or `DO4/main/main.c`. Near the top, each file lists the
node address, board type, firmware version, CAN pins and four local GPIO pins.
The `mecs_identity_t` in `app_main()` repeats the identity fields that go onto
the bus. This deliberate repetition makes it easy to compare the board label,
local wiring and announced values.

Startup then follows a visible sequence:

1. `mecs_io_node_init()` creates the portable channel settings and binds
   the local input/output function for applying a property.
2. DI4 installs GPIO edge handlers; DO4 prepares its output pins when applying each channel.
3. Each channel is initialized to its safe inactive/input state.
4. `mecs_init()` stores this board's identity and the shared CAN send callback.
5. `mecs_can_start()` starts the CAN controller.
6. `mecs_announce(..., MECS_ANNOUNCE_BOOT)` sends this board's identity.

The node main loop runs every nominal millisecond. It samples its pins, advances
local filtering/pulse/slow-PWM state, polls CAN recovery and processes received
frames. It sends replies before announcements and status. The DI4 loop also
sends the latest captured period and duty after status.

## 3. Trace the discovery request

A master calls `mecs_request_discovery()`; the shared function constructs a
standard CAN data frame with identifier `0x080` and the protocol revision.
The CAN driver copies the frame into its transmit slot.

On DI4 or DO4, the receive loop calls `mecs_is_discovery_request()`. That shared
helper checks frame type, identifier, payload size and revision. The board
application then marks an announcement for sending and later calls
`mecs_announce(&protocol, MECS_ANNOUNCE_REQUEST)`. The `protocol` instance
contains the identity that this board passed to `mecs_init()`; the shared
announce function writes those identity fields into its node-specific CAN frame.
This makes the request-to-response path visible in board code while keeping
byte order and frame layout in one library.

The master passes received announcements to `mecs_receive()`. The core validates
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
0x202. The board passes its own NODE_ADDRESS to mecs_io_decode_request(). That
shared decoder checks the exact identifier, standard-frame type, DLC, and
nonzero transaction/session fields before decoding the payload. A command for
another node returns false; the node does not pass it to mecs_io_node_request()
or change any channel state.

The CAN controller therefore sees the bus frames, while software decides which
ones belong to this node. Discovery and heartbeat are broadcasts by design.
The master applies a similar check to reply/status/measurement identifier ranges,
then matches replies to the outstanding node, transaction, session and property.

## 5. Follow an I/O setting

A dashboard or serial command is parsed by `mecs_command_parse()` from the
`mecs_command` component into a typed property. The master queues the request
and sends one transaction at a time.
On the node, `mecs_io_decode_request()` validates the frame, then
`mecs_io_node_request()` checks session, channel, property and value. It stages
the new configuration, invokes the callback supplied by this board, and commits
the value only if the hardware accepts it. `mecs_io_encode_reply()` returns the
confirmed value to the master.

Inputs filter locally and only report processed values. DI4's input_hardware.c
timestamps GPIO edges and its pwm_measurement.c calculates period and active duty.
DO4 generates PWM or slow PWM in output_hardware.c. The shared measurement
encoder/decoder carries the completed result over CAN, not individual edges.

## 6. Shared library boundaries

`mecs_core` is portable C. It knows fixed message layouts and I/O rules, but does
not include ESP-IDF or call hardware directly. Node applications pass a send
callback to `mecs_init()` and a hardware-apply callback to the I/O model.

`mecs_client` is the public master-side C API. It owns discovery, the master
heartbeat, cached node state, and one acknowledged property request at a time.
Read [MECS_CLIENT_API.md](MECS_CLIENT_API.md) before adding framework-specific code. The
master web server is kept in `MASTER/` as a test application and is not a
dependency of the public library.

`mecs_espidf` converts the portable `mecs_frame_t` into ESP-IDF TWAI frames. It
copies received data from its interrupt callback into a bounded queue. The
master-only `mecs_espidf_client` component wraps that transport with NVS session
storage and the managed `MECSClient` API. Node firmware depends only on the
transport and does not compile the master client.

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

An STM32 port can reuse `mecs_core` and provide a new CAN adapter and hardware
callbacks. An Arduino-ESP32 TWAI facade and library-packaging script are in
`SHARED/adapters/arduino` and `SHARED/tools/package_arduino.py`; test the
generated library with the selected Arduino-ESP32 core before claiming a
supported release. Analog input, more module profiles, hardware-assisted pulse
capture and persistent channel settings remain future work. The ESPHome adapter
source and package script are present, but still need a build/configuration
check against the selected ESPHome release before it is treated as a supported
integration.

## Managed MECS client (0.2.0)

Start with `SHARED/components/mecs_client/include/MECS.h` for the user-facing
node/pin API. `MECS.cpp` owns a fixed table of requested properties, confirmed
bits and the current node generation. Its scheduler sends one property at a
time, confirms each reply, and writes output enable last. Arduino glue in
`SHARED/adapters/arduino/MECSClient.cpp` supplies the clock, persistent session,
TWAI driver and optional readable logging. No sketch callback is needed.

The lower-level `mecs_client.c` still owns wire requests, heartbeat, registry,
timeouts and retries. A generation changes when a node's applied state may
have been lost. Pending requests from that node are cancelled; requests for
other nodes keep their FIFO order. This separation lets Arduino, IDF and other
framework adapters reuse the same recovery implementation.

`test_mecs_client.c` contains the original error-6 regression. It drops the first
heartbeat, sends all five servo setup requests, then verifies that the repaired
session applies mode, frequency, duty and enable without restarting the master.
`test_recovery.cpp` connects the real portable controller and node model through
a simulated CAN transport and checks physical-model state under resets, loss,
duplication, delayed replies and hardware rejection. It also checks that an
explicit OFF during a disconnection overrides a previously requested ON.

The three-decimal duty extension and the minimum report interval are specified
in `PROTOCOL.md`; customer semantics and commissioning steps are in `MECS_API.md`.
