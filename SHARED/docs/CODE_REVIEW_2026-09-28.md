# Firmware and CAN review — 2026-09-28

Reviewed the fixed MASTER/DI4/DO4 applications, portable protocol and I/O model,
C/C++ clients, ESP-IDF/Arduino transports, framework adapters, build boundaries,
and existing host checks. Changes focus on failure handling and observability;
CAN IDs, payload formats, property settings and wire revision remain unchanged.

## Findings fixed

| Priority | Finding | Change |
|---|---|---|
| High | An aborted CAN TX could leave the sole TX token unavailable if completion was lost during bus-off. | Recreate the controller after bus recovery before reclaiming TX storage; keep RX quarantined and retry failed creation with backoff. |
| High | Queued RX was assigned the processing time, allowing old heartbeats to renew a lease after an owner-task stall. | Timestamp queued frames and discard those aged 100 ms or more; clear RX on recovery. |
| High | The development master retained commands/readback across transport recovery and some node lifetime changes. | Centralize invalidation and cancellation for recovery, reboot, lost status and lost master lease; report transitions. |
| High | Receive-before-loop adapters could confirm a matching ACK after its request expired. | Apply one deadline check to both receive and retry paths, invalidate the timed-out property, and ignore replies to unsent requests. |
| Medium | Nodes overwrote their only pending reply while draining additional commands. | Preserve the reply and defer additional commands to the master's bounded retry. |
| Medium | Discovery could replace a pending boot/recovery announcement reason. | Preserve the stronger pending reason until submission. |
| Medium | A small best-effort fault queue lost diagnostics, while polling could log faults every millisecond. | Replace it with cumulative counters and bounded fault summaries, including error categories. |
| Medium | Short FD frames could enter a Classic-only protocol adapter. | Reject FD, extended, RTR and oversized frames before enqueueing. |
| Medium | ESPHome did not check the persistent session commit or handle transaction exhaustion. Arduino retried failed session storage every loop. | Require successful commit, rotate ESPHome sessions through the shared client, and limit persistence retry frequency. |
| Low | Arduino transport recovery state survived `end()`/`begin()`. | Reset lifecycle state, reject unsupported transmit formats and clear recovered RX. |
| Low | A FreeRTOS tick rate below 1 kHz could turn the node loop delay into zero ticks. | Use at least one tick; retain 1 kHz board defaults. |
| Low | Default ESP-IDF component selection compiled unrelated master and networking code for nodes. | Build only the declared dependency graph from `main`. |

## Architecture

Retained portable protocol/model code in `mecs_core`, customer state management
in `mecs_client`, and GPIO/LEDC/capture behavior in the board folders. The test
master has its own transaction and readback policy; shared timeout/invalidation
helpers now remove duplication within that implementation. Replacing the whole
master with the managed client would change interactive command behavior and
is not necessary for these fixes.

The CAN adapter uses one persistent asynchronous TX slot. Returning true from
send means accepted, not executed remotely. Ownership is released by completion
or after the old controller is disabled and deleted. The supported driver API
and its buffer lifetime requirements are described in the
[ESP-IDF 5.5 TWAI reference](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c3/api-reference/peripherals/twai.html).
The recovery implementation was also checked against the source bundled in the
actual `espressif/idf:v5.5` build image.

The ESPHome persistence change checks the boolean commit result exposed by its
[preferences API](https://github.com/esphome/esphome/blob/dev/esphome/core/preferences.h).
It does not establish compatibility with an untested ESPHome release or its
underlying CAN driver's queue/retry behavior.

## Diagnostics

See [DEVELOPMENT.md](DEVELOPMENT.md#can-diagnostics-and-recovery). Default logs
cover CAN faults, recovery, master sessions, lease loss, node loss, hardware
faults and command rejections. DEBUG adds CAN IDs/DLCs and transaction details.
The dashboard and `/api/state` include cumulative CAN counters and controller
error state. ISRs never format logs.

## Validation and limits

- Portable CMake/CTest: `client`, `recovery`, and new `can` suites pass.
- The CAN suite compiles the production adapter with a small RTOS/driver model.
  It checks copied TX lifetime, busy/rejected submissions, overflow, stale RX,
  FD rejection, bounded logging, RX quarantine, aborted TX recovery and retry
  after controller allocation failure. It does not emulate electrical CAN,
  real interrupt concurrency or FreeRTOS scheduling.
- Client regressions cover late ACKs, the final retry deadline and clock wrap.
- MASTER, DO4 and DI4 build with ESP-IDF 5.5, with the reduced dependency graph.
- Arduino `ServoRecovery` compiles using the installed ESP32 3.3.11 core and a
  package built from canonical sources. Arduino emitted only installed-library
  metadata category warnings.
- Arduino and ESPHome packagers completed; dashboard JavaScript syntax passed.
- ESPHome runtime compilation remains pending; no ESPHome compiler is installed.
- Physical fault injection, GPIO/LEDC timing, output inactivity after loss and
  dashboard rendering on a running master have not been exercised in this review.

The workspace contains no usable Git metadata, so review changes were compared
against a local pre-edit snapshot. No firmware was flashed. The source commit is prepared in the existing clean
`Weyla/MECS` checkout after confirming its files match the pre-edit snapshot.
