# MIO wire protocol specification

| Document control | Value |
|---|---|
| Document ID | MIO-PROT-001 |
| Revision / date | 3.0 / 2026-09-27 |
| Software / wire | 0.3.0 / revision 3 |
| Scope | Discovery, digital I/O, property transactions and liveness |

## 1. Bus profile

Classic CAN, 11-bit data frames, 500,000 bit/s, maximum eight payload bytes.
All multi-byte integers are little-endian. RTR and extended frames are ignored. There
is one master (logical address 0); expansion addresses 1–127 must be unique.
The example master stores up to 16 discovered nodes. CAN CRC/arbitration handle
link integrity; CAN ACK does not confirm application execution.

Wire revision 3 is intentionally incompatible with revisions 1 and 2. An unsupported
revision in discovery, heartbeat or status is rejected. Commands/replies inherit
the session established by a compatible master heartbeat.

## 2. Identifiers and payloads

`N` is an expansion address 1–127. The lower numeric ID wins CAN arbitration.
All unused identifiers are ignored. Every assigned message requires exact DLC.

| Identifier | Direction | DLC | Meaning |
|---|---|---:|---|
| `0x080` | Master → all | 1 | Discovery request |
| `0x081` | Master → all | 3 | Master heartbeat/session |
| `0x100 + N` | Node → master | 8 | Identity announcement |
| `0x200 + N` | Master → node N | 8 | Property request |
| `0x280 + N` | Node N → master | 8 | Property reply |
| `0x300 + N` | Node N → master | 8 | Status/liveness |
| `0x380 + N` | Node N → master | 8 | PWM measurement, one input channel |

### Discovery request

Byte 0 is wire revision `3`. Each compatible node schedules an announcement.
Responses have different CAN identifiers, allowing responders to arbitrate.

### Announcement

| Byte | Meaning |
|---:|---|
| 0 | Wire revision 3 |
| 1–2 | Board type: 1 = old discovery demo, 2 = DI4, 3 = DO4; 0 invalid |
| 3–5 | Firmware major, minor, patch; current version 0.3.0 |
| 6 | Reason: 0 boot, 1 requested/periodic refresh, 2 CAN recovery |
| 7 | Reserved, must be 0 |

Node address comes from the identifier. Unknown nonzero board types can be
listed, but the master enables I/O controls only for DI4/DO4. Nodes announce at
boot, on request, on CAN recovery and approximately every 5 seconds. The master
also requests discovery at startup, on demand and approximately every 5 seconds.

Example node 1 output announcement: `101#0303000003000000`.

### Master heartbeat

Byte 0 = wire revision 3; bytes 1–2 = nonzero master session.
The master submits a heartbeat every 250 ms. A node considers the master offline after
1,500 ms without one. A changed session immediately stops the outputs and
clears transaction history. A heartbeat alone never enables outputs.

The master persists an incrementing 16-bit session in NVS before using it,
skipping zero. It advances on reboot and transaction-space exhaustion. This is
an accidental stale-message guard, not authentication or a globally unique ID;
a full 16-bit wrap or erased NVS can reuse an old session.

### Property request

| Byte | Meaning |
|---:|---|
| 0 | Property ID; bit 7 set for read, clear for write |
| 1 | Channel 0–3, or 255 for node-wide properties |
| 2–3 | Nonzero transaction number |
| 4–5 | Nonzero master session, matching the most recent heartbeat |
| 6–7 | Unsigned value for a write; ignored for a read |

### Property reply

| Byte | Meaning |
|---:|---|
| 0 | Echo of request property byte, including read flag |
| 1 | Result code |
| 2–3 | Echo of transaction |
| 4–5 | Echo of session |
| 6–7 | Confirmed property value on success; zero on errors |

The channel is associated through the master's outstanding request. Replies are
accepted only when node, session, transaction and property all match it. A write
is committed in the portable model only after the hardware callback accepts it.
A successful trigger reply confirms that the pulse started, not that it is still
active. Status reports the current gate afterward.

### Status

| Byte | Meaning |
|---:|---|
| 0 | Wire revision 3 |
| 1 | Flags: bit 0 reserved zero, bit 1 master heartbeat alive, bit 2 hardware fault; other bits zero |
| 2 | Raw electrical pin levels in bits 0–3; upper bits zero |
| 3 | DI4: filtered logical activity. DO4: commanded output gates. Bits 0–3 only |
| 4–5 | Random 16-bit expansion boot marker |
| 6–7 | Incrementing 16-bit status sequence; wraps naturally |

Status arrives at least every 500 ms under an available bus. The master marks
a node stale after 1,500 ms without valid status. Old values stay visible as
historical data, but UI live states become unavailable and controls are disabled.
The sequence is diagnostic; this release does not reject sequence gaps. The boot
marker helps detect missed boot announcements; it is not collision-free identity.
PWM raw level is only an instantaneous GPIO sample, not a duty-cycle measurement.

### PWM measurement

| Byte | Meaning |
|---:|---|
| 0 | Wire revision 3 |
| 1 | Channel 0–3 in bits 0–1; bit 7 valid; bits 2–6 zero |
| 2–5 | Electrical rising-edge period, microseconds (uint32) |
| 6–7 | Logical active duty, percent × 100 (uint16; 0–10000) |

Invalid measurements have zero period and duty. Active-low selection complements
high-time duty without changing period. Frequency is derived as 1,000,000 / period.
Frames describe the latest complete cycle, not every cycle or an average. They
follow status reporting for channels configured for PWM measurement. The master
invalidates stale telemetry after 1500 ms and on expansion restart.

## 3. Property dictionary

Use channel 255 for `report` and `report_ms`; all others use 0–3. Values on CAN
are unsigned integers. Console and web duty use percent with up to two decimal
places; period uses seconds with up to one decimal place. The schema `scale`
converts display to wire units: wire = displayed value × scale. Other scales are 1.
Read-only properties reject writes. Unsupported roles/channels/ranges return errors.

| ID | Name | Applies to | Values / limits on wire |
|---:|---|---|---|
| 1 | `polarity` | Both | 0 active-high, 1 active-low |
| 2 | `filter` | DI4 | 0 none, 1 stable debounce, 2 PWM measurement |
| 3 | `filter_ms` | DI4 | 1–1000 ms |
| 4 | `pull` | DI4 | 0 floating, 1 pull-up, 2 pull-down |
| 5 | `report` | Both | 0 periodic, 1 on change, 2 periodic + change |
| 6 | `report_ms` | Both | 20–500 ms |
| 7 | `mode` | DO4 | 0 digital, 1 hardware PWM, 2 timed pulse, 3 slow PWM |
| 8 | `frequency` | DO4 | 10–10000 Hz |
| 9 | `duty` | DO4 | 0–10000; scale 100 (0–100%) |
| 10 | `value` | DO4 | 0 inactive, 1 enabled; use trigger in pulse mode |
| 11 | `pulse_ms` | DO4 | 10–60000 ms |
| 12 | `trigger` | DO4 | Write 1 starts/restarts pulse; read returns 0 |
| 13 | `pin` | Both | Read-only GPIO number |
| 14 | `period` | DO4 | 1–36000; scale 10 (0.1–3600 seconds) |

Defaults: DI4 active-low, pull-up, stable debounce 20 ms. DO4 active-high
(build-configurable), digital, disabled, PWM 1000 Hz/50%, slow period 10 s,
pulse 100 ms. Both report periodic + change at 100 ms. Changes are volatile.
Input reconfiguration resets filter/capture history. Output settings are editable
while enabled; see the operation manual for phase and pulse restart rules.

## 4. Transactions, retries and failure behavior

The master runs at most one outstanding request across the bus, with a 32-entry
user command queue. Readback after discovery is lower priority. Submission is
nonblocking. An unanswered request is retried with the same transaction up to
three accepted transmissions at 300 ms spacing, with a total 1,500 ms limit.
Successful CAN submission still does not prove remote execution.

The node remembers its last request and reply within the current session.
An identical retry returns that reply without repeating an action. Reusing the
same transaction for different content or sending a lower transaction is
rejected. Monotonic transaction ordering plus one outstanding request avoids
needing a large replay cache. Before transaction 65535 would wrap, the master
advances its session, clears the user queue and clears output gates.

A timeout means **outcome unknown**: an operation might have executed while its
reply was lost. The master invalidates that property's cached value and drops
remaining queued user operations. Refresh/read status before retrying a pulse
with a new transaction. No exactly-once guarantee survives expansion power loss.
Reboots invalidate cached settings and queued intentions. Lost communication
clears all output gates and pending pulses; recovery does not restore them.

| Code | Meaning |
|---:|---|
| 0 | Success |
| 1 | Unsupported/read-only property or action invalid for mode |
| 2 | Invalid channel scope |
| 3 | Value outside range |
| 4 | Master heartbeat absent/expired |
| 5 | Hardware application failed or hardware fault latched |
| 6 | Session mismatch |
| 7 | Stale/reused/zero transaction |

Hardware errors latch a fault and attempt to drive all outputs inactive. A fault
requires a node reboot; it is not cleared by an enable command. An electrical hardware fault
can prevent the commanded inactive level, so this is not a safety-rated guarantee.

## 5. Reporting and timing

Digital sampling, timed pulses and slow PWM run in a nominal 1 ms node task.
Stable debounce uses elapsed monotonic time. Slow PWM advances cycle boundaries
without accumulating task jitter; actual edges remain subject to scheduling.
PWM measurement uses GPIO edge interrupts and microsecond timestamps. The target
range is 1–10000 Hz; accepted periods are 90–1000000 µs, with a small upper-end
margin for timestamp jitter. Both electrical phases must measure at least 4 µs.
This is validation, not a timing-accuracy guarantee. Constant 0%/100% signals
cannot supply a period and are invalid. Missing cycles expire after the greater
of 100 ms or three measured periods. A per-channel interrupt-rate guard disables
capture above 240 observed edges per 10 ms window and retries after 100 ms.
All four inputs can select measurement, but simultaneous 10 kHz loading and
measurement accuracy require a physical bench test. Interrupt latency, missed
edges and flash/cache stalls can distort or alias signals; validation cannot
detect every incorrect measurement. Hardware capture is the intended production
path when stronger timing guarantees are required.

On-change reports have a minimum 20 ms spacing. Periodic reports use `report_ms`.
Periodic + change enables both. Even change-only mode emits mandatory 500 ms
status for liveness. Multiple changes between reports may collapse to the final
state; the stream is not an edge-event recorder. Replies take priority over status.
PWM generation is performed by LEDC and is independent of CAN update intervals.
