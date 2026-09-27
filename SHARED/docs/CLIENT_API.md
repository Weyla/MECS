# MIO client API

| Document control | Value |
|---|---|
| Document ID | MIO-SDK-001 |
| Revision / date | 1.0 / 2026-09-27 |
| Audience | Master application developers |
| Scope | Portable client API; excludes the test dashboard and web server |

## 1. Purpose

The MIO client lets a master application use expansion modules without
constructing CAN identifiers or payloads. It owns node discovery, heartbeat,
the node registry, cached values, and the request/acknowledgement sequence.
It does not start a CAN controller; a small framework adapter supplies
transmission and delivers received frames.

The web dashboard in `MASTER/` is a test application. It is not part of the
client component and is not needed by customer firmware.

## 2. Portable boundary

`mio_client` and `mio_core` use fixed-size C data structures and have no
ESP-IDF, Arduino, ESPHome or operating-system dependency. Their contract is:

1. The application initializes a CAN transport and chooses a nonzero master
   session.
2. The application calls `mio_client_begin()` with a function that copies and
   queues one `mio_frame_t` for CAN transmission.
3. The application calls `mio_client_loop()` regularly, passing a monotonic
   millisecond count.
4. For each received standard CAN data frame, the application calls
   `mio_client_receive()` in its normal task/loop context.
5. The client emits typed events and updates a fixed-size cache. Applications
   read cached values through `mio_client_node()`.

Never call the client from an ISR. Serialize calls to one client instance.
Event callbacks run synchronously inside `mio_client_receive()` or
`mio_client_loop()` and must not block or re-enter the same client.

## 3. Minimal usage

```c
static mio_client_t io;

static bool send_can_frame(void *context, const mio_frame_t *frame) {
  (void)context;
  /* Copy frame into the platform's CAN transmit queue here. */
  return board_can_send(frame);
}

void application_start(void) {
  const mio_client_config_t config = {
      .send_frame = send_can_frame,
      .transport_context = NULL,
      .session = load_and_increment_persistent_session(),
  };
  mio_client_begin(&io, &config, on_mio_event, NULL);
}

void application_loop(void) {
  const uint32_t now = monotonic_milliseconds();
  mio_frame_t frame;
  while (board_can_receive(&frame)) {
    mio_client_receive(&io, &frame, now);
  }
  mio_client_loop(&io, now);
}
```

The transport callback returns true when the frame is accepted into the local
transmit queue. This does not mean the expansion node has executed the command.
Wait for `MIO_CLIENT_EVENT_REQUEST_CONFIRMED` or a rejected/timeout event.

## 4. Configure and use channels

`mio_client_discover()` requests fresh announcements; discovery also starts
automatically and repeats periodically. Read a cached snapshot after the node
announces and reports status:

```c
const mio_client_node_t *outputs = mio_client_node(&io, 1);
if (mio_client_node_online(outputs, now)) {
  /* Set channel 0's output-enable gate. */
  bool accepted = mio_client_set(&io, 1, 0, MIO_PROP_VALUE, 1);
}
```

`mio_client_set()` returning true means the request entered the client's
bounded 16-request FIFO. It does not mean the physical output has changed. The
client sends requests one at a time and reports each confirmed value through
the event callback. `mio_client_get()` reads a setting from the node. Global
settings use `MIO_GLOBAL_CHANNEL` (255). If the FIFO is full or the node is
offline, the function returns false and no request is accepted.

Cached fields are indexed by channel and `mio_property_t`. Check the matching
bit in `node->valid[channel]` before reading `node->value[channel][property]`.
The status lease expires after 1500 ms without a status frame; do not treat an
old cached value as current when `mio_client_node_online()` is false.

For PWM, the encoded duty unit is one hundredth of a percent: 12.5% is passed as
1250. Slow-PWM period uses tenths of a second: 2.5 seconds is passed as 25.
Input filtering and PWM measurement execute on the input node; CAN transports
the selected settings and completed measurements, not every GPIO edge.

## 5. Session and safe outputs

Choose a nonzero session and persist/increment it before every master reboot.
The node uses the session to reject commands from a previous master run and
uses heartbeat loss to return outputs to their inactive state. The client
cannot persist data for every possible framework, so the application supplies
the session. When `mio_client_needs_new_session()` is true, save a new nonzero
session and call `mio_client_set_session()` before sending more commands.
Transaction IDs are never reused within one session.

## 6. Framework adapters

The adapter must not reimplement discovery or property transactions:

| Framework | Adapter responsibility |
|---|---|
| ESP-IDF | Convert `mio_frame_t` to/from TWAI and call client loop/receive from the owner task. |
| Arduino IDE | Provide the same callbacks around the Arduino-ESP32 CAN/TWAI driver and call loop/receive from `loop()`. |
| ESPHome | Bind a client to ESPHome's configured `canbus` component, then expose selected node channels as ESPHome entities and actions. |

Framework-specific entity models remain in adapter packages. Web server code,
Wi-Fi, HTML, serial consoles and dashboards are application features, not
client-library dependencies.

## 7. Custom extensions

The current client exposes the version-3 digital and PWM properties defined by
`mio_core`. Custom application helpers can use the typed property API without
handling CAN. A future module profile should publish capability metadata and
provide a registration interface before custom channel types are exposed
through the same high-level API. Until then, adding a new on-wire property
requires a coordinated protocol and node-firmware change; it is not a master-
only extension.
