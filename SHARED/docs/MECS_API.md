# MECS managed node/pin API

| Document control | Value |
|---|---|
| Document ID | MECS-SDK-002 |
| Revision | 1.0, 2026-09-27 |
| Applies to | MECSClient 0.2.0; DI4/DO4 firmware 0.3.1 |
| Audience | Application developers using Arduino IDE or ESP-IDF |

## 1. Object model

```cpp
#include "MECSClient.h"
MECSClient can;
auto outputs = can.node(1);
auto inputs = can.node(2);
auto servo = outputs.pin(3);
auto button = inputs.pin(0);
```

A client owns the CAN connection and requested settings. A node identifies a
remote address. A pin identifies a channel on that node. The prototype maps
channels 0–3 to GPIO0–GPIO3. These numbers do not identify master GPIOs.
Handles refer to the client's storage; copying a handle does not create another
connection. Keep the client alive as long as its handles exist. The controller
supports up to 16 registered nodes and four channels per node, using fixed memory.
Node addresses remain 1–127. Invalid channels or an exhausted registry cause
setters to return false; they never address a different channel.

Only one master may control a bus. Call the library from a single task. Keep
`can.loop()` running frequently; long blocking delays prevent both receive
processing and heartbeat transmission. No user event callback is required.
`can.enableLogging(Serial)` enables readable diagnostics; logging is optional.

## 2. Configuration and confirmation

```cpp
void setup() {
  Serial.begin(115200);
  can.enableLogging(Serial);
  if (!can.begin(4, 5)) return; // TX, RX; default 500 kbit/s.
  servo.setMode(MECS::PWM);
  servo.setPwm(50, 7.500);
  servo.turnOn();
  button.setDebounce(20);
  button.setResistor(MECS::PullUp);
  button.setActiveLow(true);
  inputs.setReporting(20, 500, true);
}
```

Setters record requested state, including before nodes have connected. A true
return means the argument was accepted locally. `pin.ready()` becomes true only
when all requested settings have been acknowledged by the current node, its
status is fresh, and it reports no hardware fault. `node.ready()` covers all of
that node's registered settings. `node.online()` only indicates fresh status;
it does not imply that configuration is complete.

The library sends one setting at a time and waits for its reply. After a reset
or recovery, it first requests an inactive output, then mode and parameters,
and finally the application's requested enable state. Mode therefore precedes
PWM frequency and duty regardless of the order of setup calls. `setPwm()` also
selects PWM mode, so the explicit `setMode(MECS::PWM)` above is optional.

Repeated calls with identical values produce no additional writes. If a value
changes repeatedly while a reply is pending, the latest requested value is
retained; intermediate unsent values are coalesced. An older reply never marks
a newer requested value confirmed. Live duty/frequency edits preserve the output
enable state. `turnOff()` remains available after a rejected parameter update.

These setters are state assignments, not a timed sequence. Calling `turnOn()`
and then `turnOff()` before `loop()` services them requests OFF, not a pulse.
Use node-side timing for time-critical waveforms. Timed-pulse triggering remains
available in the advanced protocol API; the managed API does not automatically
replay one-shot actions.

## 3. Pin operations

| Operation | Units / behavior |
|---|---|
| `setMode(MECS::DIGITAL_OUTPUT)` | Constant logical output, controlled by on/off |
| `setPwm(hertz, percent)` | 10–10,000 Hz; duty 0.000–100.000%, rounded to 0.001% |
| `setDuty(percent)` | Change duty of the configured PWM/slow-PWM output |
| `setSlowPwm(seconds, percent)` | Period 0.1–3,600.0 s, rounded to 0.1 s |
| `turnOn()` / `turnOff()` | Explicit requested output-enable state |
| `setActiveLow(bool)` | Active-low when true, active-high when false |
| `setMode(MECS::DIGITAL_INPUT)` | Unfiltered sampled logical input |
| `setDebounce(milliseconds)` | Stable debounce, 1–1,000 ms; also selects debounce mode |
| `setMode(MECS::PWM_INPUT)` | Node-local PWM measurement |
| `setResistor(MECS::Floating)` | No internal pull resistor |
| `setResistor(MECS::PullUp)` | Internal pull-up |
| `setResistor(MECS::PullDown)` | Internal pull-down |
| `valid()` / `value()` | Validity and cached logical input state |
| `pwm()` | Measurement with `valid`, `frequencyHz`, `periodSeconds`, `dutyPercent` |
| `lastError()` | Latest local/protocol error, including offline/hardware failure |

`setMode()` defaults an unspecified polarity to active-high. Set active-low
explicitly for a switch to ground. Missing/NaN/infinite/out-of-range numeric
settings are rejected locally. A role mismatch (for example, PWM output on a
DI4 node) is rejected and cannot enable that channel.

The three-decimal API represents the requested duty precisely on CAN. The
LEDC timer rounds it to an available hardware counter step. This is not a claim
of 0.001% physical waveform accuracy at every frequency. PWM input measurement
continues to use the existing two-decimal duty telemetry and the prototype's
GPIO edge-capture timing limits.

## 4. Input validity and reporting

```cpp
void loop() {
  can.loop();
  if (!button.valid()) {
    servo.turnOff(); // This application's chosen behavior for a missing input.
    return;
  }
  servo.setPwm(50, button.value() ? 5.000 : 12.500);
  servo.turnOn();
}
```

`value()` is a cached, nonblocking logical reading. It returns false if invalid;
check `valid()` when false must be distinguished from a disconnected input.
Inputs wait for a fresh status after their configuration acknowledgements.
A node's status expires after 1,500 ms. PWM measurements additionally require
valid, fresh measurement telemetry.

`node.setReporting(minimumMs, maximumMs, onChange)` controls both periodic and
change reporting. With changes enabled, a changed state may be sent after the
minimum gap. A periodic snapshot is sent no later than the maximum interval
when the transport is available. With changes disabled, only the periodic
interval applies. This is latest-state reporting, not an edge/event queue.
Several changes within the minimum interval can be combined into one snapshot.

Current bounds are `0 <= minimumMs <= maximumMs`, with maximum 20–500 ms.
The 500 ms upper bound preserves the current shared status/liveness protocol.
This is an explicit validation limit, not silent clipping. The bus transport
may delay or lose a frame; the configured interval is a node scheduling target.

## 5. Reset, disconnection and session handling

The observed failure was a startup race: the node announced before receiving a
master heartbeat. Requests arrived with a session the freshly booted node did
not yet know and were rejected with error 6. The previous sketch marked setup
complete when requests entered the queue. Later duty writes succeeded, but mode,
frequency and enable had never been applied.

The corrected transport-independent client:

1. Sends a heartbeat promptly when a node boots or reports no live master.
2. Holds new requests until node status reports a live master.
3. Retries session/offline rejections after refreshing the heartbeat, preserving
   the original transaction and its bounded deadline.
4. Cancels only the affected node's old queued/in-flight commands on a reset,
   recovery announcement, changed boot identity, or detected lease loss.
5. Expires an active request even when the transport accepts no transmission.
6. Completes master-session rollover without continuously extending its wait.

The managed MECS layer retains requested settings in master RAM and replays
unconfirmed settings after these events. Hardware/property/range rejections
remain visible and prevent enabling an incompletely configured channel.
Temporary failures are retried with backoff. Built-in transports use single-shot
transmission so an old command is not left in an indefinite controller retry
backlog; the protocol owns request retries. Arduino additionally restarts its
TWAI driver after bus-off recovery and invalidates old work.

On the node, missing master heartbeats still clear every enable gate after
1,500 ms. Heartbeats alone never re-enable outputs. A running MECS master that
previously received `turnOn()` explicitly reapplies that requested state after
reconnection. A `turnOff()` while disconnected changes the retained request to
OFF. Resetting the master erases its RAM requests; its sketch must register them
again in `setup()`. The persisted master session changes on startup.

Firmware also prevents replaying an old successful enable acknowledgement after
a lease loss has physically stopped the output. Retries are safe for state
assignments; one-shot operations must not be automatically reissued as state.

## 6. Installation and compatibility

For Arduino IDE, install `MECSClient.zip` through **Sketch → Include Library →
Add .ZIP Library…**. Open **File → Examples → MECSClient → ServoRecovery** for
the test with only node 1 and the servo on channel 3. `BasicMaster` adds the DI4
input; `DO4` and `DI4` show all channel options. For the ESP32-C3 native USB
connector, enable **USB CDC On Boot** to see `Serial` output.

For ESP-IDF, include `MECSClient.h` and add `mecs_espidf_client` to the application's
component requirements. Call `can.begin(txGpio, rxGpio)` once, register the same
node and pin settings, and call `can.loop()` regularly from one FreeRTOS task.
The IDF adapter starts TWAI at 500 kbit/s by default, initializes NVS and
persists the master session. Pass a third `bitrate` argument to select another
supported rate. If NVS needs recovery, `begin()` returns its error; the library
does not erase the partition because that could also contain application data.
The complete input-to-servo example is
[`examples/esp-idf/mecs_master/main/main.cpp`](../../examples/esp-idf/mecs_master/main/main.cpp).

Upgrade DO4 and DI4 to 0.3.1 when using precise duty and configurable minimum
report spacing. Old firmware rejects these new properties; the managed API
leaves the output inactive rather than presenting setup as successful. The
existing revision-3 duty property and heartbeat format remain compatible.
Do not leave an older separately installed `MECSClient` library alongside the
new package if it causes duplicate-library resolution; the MECS package already
contains a compatibility facade for old Arduino sketches.

Advanced C event callbacks carry a `uint32_t` property value. The managed
Arduino and ESP-IDF APIs do not require applications to write event callbacks;
optional logging is available through `enableLogging()`.

`SHARED/components/mecs_client` contains the allocation-free C++ controller and
node/pin objects without Arduino or web-server dependencies. ESP-IDF's
`mecs_espidf_client` component wraps it with the same `MECSClient` API; advanced
applications can still provide their own transport/session adapter. The current
ESPHome entity adapter still exposes its earlier switch and binary-sensor
interface; it does not yet expose these managed configuration objects through
YAML.

## 7. Verification and acceptance

Automated tests run the actual client, CAN codec and node I/O model in a
simulated network. They cover the original session rejection, 300 repeated
resets/recoveries, missing boot announcements, lost replies, delay/duplication,
transport refusal, session rollover, hardware failure, input validity, repeated
setters and precise-duty boundaries. Assertions check the node's actual model
configuration and enable gate, not only confirmed log output.

```sh
cmake -S SHARED/tests -B /tmp/mecs-tests
cmake --build /tmp/mecs-tests
ctest --test-dir /tmp/mecs-tests --output-on-failure
```

For board acceptance, flash the updated node and master, run `ServoRecovery`,
reset node 1 repeatedly at different times, and verify the last requested duty
returns without resetting the master. Disconnect CAN long enough to exceed the
lease, check inactive output, reconnect, then verify recovery. Observe GPIO3
with a scope/logic analyzer to distinguish correct pulses from servo power or
mechanical problems. Repeat with DI4 traffic active.

Host tests and firmware builds do not establish electrical reliability or servo
movement. The protocol has finite buffers, deadlines and 16-bit boot/session
identities; it does not promise communication through a disconnected or faulty
bus. Hardware acceptance remains a separate required verification step.
