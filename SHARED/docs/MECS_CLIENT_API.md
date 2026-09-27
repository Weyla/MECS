# MECS advanced C client API

| Document control | Value |
|---|---|
| Document ID | MECS-SDK-001 |
| Revision / date | 1.0 / 2026-09-27 |
| Audience | Master application developers |
| Scope | Portable client API; excludes the test dashboard and web server |

## 1. Purpose

The MECS client lets a master application use expansion modules without
constructing CAN identifiers or payloads. It owns node discovery, heartbeat,
the node registry, cached values, and the request/acknowledgement sequence.
It does not start a CAN controller; a small framework adapter supplies
transmission and delivers received frames.

The web dashboard in `MASTER/` is a test application. It is not part of the
client component and is not needed by customer firmware.

## 2. Portable boundary

`mecs_client` and `mecs_core` use fixed-size C data structures and have no
ESP-IDF, Arduino, ESPHome or operating-system dependency. Their contract is:

1. The application initializes a CAN transport and chooses a nonzero master
   session.
2. The application calls `mecs_client_begin()` with a function that copies and
   queues one `mecs_frame_t` for CAN transmission.
3. The application calls `mecs_client_loop()` regularly, passing a monotonic
   millisecond count.
4. For each received standard CAN data frame, the application calls
   `mecs_client_receive()` in its normal task/loop context.
5. The client emits typed events and updates a fixed-size cache. Applications
   read cached values through `mecs_client_node()`.

Never call the client from an ISR. Serialize calls to one client instance.
Event callbacks run synchronously inside `mecs_client_receive()` or
`mecs_client_loop()` and must not block or re-enter the same client.

## 3. Minimal usage

```c
static mecs_client_t io;

static bool send_can_frame(void *context, const mecs_frame_t *frame) {
  (void)context;
  /* Copy frame into the platform's CAN transmit queue here. */
  return board_can_send(frame);
}

void application_start(void) {
  const mecs_client_config_t config = {
      .send_frame = send_can_frame,
      .transport_context = NULL,
      .session = load_and_increment_persistent_session(),
  };
  mecs_client_begin(&io, &config, on_mecs_event, NULL);
}

void application_loop(void) {
  const uint32_t now = monotonic_milliseconds();
  mecs_frame_t frame;
  while (board_can_receive(&frame)) {
    mecs_client_receive(&io, &frame, now);
  }
  mecs_client_loop(&io, now);
}
```

The transport callback returns true when the frame is accepted into the local
transmit queue. This does not mean the expansion node has executed the command.
Wait for `MECS_CLIENT_EVENT_REQUEST_CONFIRMED` or a rejected/timeout event.

## 4. Configure and use channels

`mecs_client_discover()` requests fresh announcements; discovery also starts
automatically and repeats periodically. Read a cached snapshot after the node
announces and reports status:

```c
const mecs_client_node_t *outputs = mecs_client_node(&io, 1);
if (mecs_client_node_online(outputs, now)) {
  /* Set channel 0's output-enable gate. */
  bool accepted = mecs_client_set(&io, 1, 0, MECS_PROP_VALUE, 1);
}
```

`mecs_client_set()` returning true means the request entered the client's
bounded 16-request FIFO. It does not mean the physical output has changed. The
client sends requests one at a time and reports each confirmed value through
the event callback. `mecs_client_get()` reads a setting from the node. Global
settings use `MECS_GLOBAL_CHANNEL` (255). If the FIFO is full or the node is
offline, the function returns false and no request is accepted.

Cached fields are indexed by channel and `mecs_property_t`. Check the matching
bit in `node->valid[channel]` before reading `node->value[channel][property]`.
The status lease expires after 1500 ms without a status frame; do not treat an
old cached value as current when `mecs_client_node_online()` is false.

For PWM, the encoded duty unit is one hundredth of a percent: 12.5% is passed as
1250. Slow-PWM period uses tenths of a second: 2.5 seconds is passed as 25.
Input filtering and PWM measurement execute on the input node; CAN transports
the selected settings and completed measurements, not every GPIO edge.

## 5. Session and safe outputs

Choose a nonzero session and persist/increment it before every master reboot.
The node uses the session to reject commands from a previous master run and
uses heartbeat loss to return outputs to their inactive state. The client
cannot persist data for every possible framework, so the application supplies
the session. When `mecs_client_needs_new_session()` is true, save a new nonzero
session and call `mecs_client_set_session()` before sending more commands.
Transaction IDs are never reused within one session.

## 6. Framework adapters

The adapter must not reimplement discovery or property transactions:

| Framework | Adapter responsibility |
|---|---|
| ESP-IDF | `mecs_espidf_client` supplies `MECSClient`, which starts TWAI, persists sessions and services frames from `loop()`. Lower-level C applications can include `mecs_espidf_client.h` and call `mecs_espidf_client_poll()` to drain received frames and advance client timers. |
| Arduino IDE | Provide the same callbacks around the Arduino-ESP32 CAN/TWAI driver and call loop/receive from `loop()`. |
| ESPHome | Bind a client to ESPHome's configured `canbus` component, then expose selected node channels as ESPHome entities and actions. |

Framework-specific entity models remain in adapter packages. Web server code,
Wi-Fi, HTML, serial consoles and dashboards are application features, not
client-library dependencies.

## 7. Custom extensions

The current client exposes the version-3 digital and PWM properties defined by
`mecs_core`. Custom application helpers can use the typed property API without
handling CAN. A future module profile should publish capability metadata and
provide a registration interface before custom channel types are exposed
through the same high-level API. Until then, adding a new on-wire property
requires a coordinated protocol and node-firmware change; it is not a master-
only extension.

## Managed node/pin API

For configuration retained across node resets, use the [MECS node/pin API](MECS_API.md). The C API below remains a request/response interface and does not retain application intentions. Property values and C callback values are now `uint32_t` to carry precise duty.
