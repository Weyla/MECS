# Verification record

| Document control | Value |
|---|---|
| Document ID | MECS-TEST-001 |
| Revision / date | 3.2 / 2026-09-27 |
| Firmware / wire | 0.3.1 / 3 |
| Hardware roles | Master; node 1 DO4; node 2 DI4 |

## 1. Scope and build coverage

This record distinguishes source-level checks, successful builds and physical
bench results. A passing host test or firmware build does not establish pin
behavior, timing accuracy or electrical safety.

The ESP-IDF projects require ESP-IDF 5.5 or newer. The CAN transport uses the
on-chip TWAI node API introduced in that release. The GitHub workflow
`.github/workflows/firmware-builds.yml` builds MASTER, DO4 and DI4 with the
minimum supported ESP-IDF release. The workflow was added with this revision;
its first hosted run is still pending.

| Project | Current evidence | Result |
|---|---|---|
| Portable client tests | Local CMake/CTest run on 2026-09-27; see section 2 | Pass |
| MASTER, DO4, DI4 | Local build in the Espressif ESP-IDF 5.5 container on 2026-09-27 | Pass |
| `examples/esp-idf/mecs_master` | Uses GitHub Component Manager dependencies; not built in this workspace | Pending |
| ESPHome example | No ESPHome compiler is installed in this workspace | Pending |

The same minimum-version builds are configured in GitHub Actions; its first
hosted run is pending. Revision 3.1 of this manual recorded successful
ESP-IDF 6.1 builds, but the exact source revision for that record is not
available in this workspace.

## 2. Portable software checks

The portable library was built with warnings treated as errors, then both CTest
targets passed:

```sh
cmake -S SHARED/tests -B /tmp/mecs-build
cmake --build /tmp/mecs-build --parallel
ctest --test-dir /tmp/mecs-build --output-on-failure
```

| Test | Coverage | Result |
|---|---|---|
| `client` | Frame validation, discovery and client behavior | Pass |
| `recovery` | Reboots, missing/duplicate frames, transport recovery, hardware faults, precise PWM, and retained setting errors | Pass |

These are deterministic host simulations of the portable client and node
model. They do not execute FreeRTOS scheduling, the ESP-IDF TWAI driver, GPIO,
LEDC or the ESPHome runtime.

## 3. Hardware acceptance

The user previously reported that the SG90 servo moved after a firmware flash.
That observation predates the current fixes and is not a bench result for this
source revision. No physical acceptance check has been repeated for this tree.

| ID | Check | Result |
|---|---|---|
| H01 | Flash matching revision 3 master and node firmware | Pending |
| H02 | Discover DO4 at address 1 and DI4 at address 2 | Pending |
| H03 | Confirm property reads/writes and decimal PWM duty | Pending |
| H04 | Change an enabled output value without disabling its gate | Pending |
| H05 | Measure input PWM frequency and duty against a signal generator | Pending |
| H06 | Check all four input capture channels through 10 kHz | Pending |
| H07 | Confirm each output returns inactive after master heartbeat loss | Pending |
| H08 | Verify slow PWM period and duty with a logic analyzer | Pending |
| H09 | Confirm polarity, GPIO readings, Wi-Fi and dashboard behavior | Pending |
| H10 | Confirm ESPHome input invalidation and output indication after node loss | Pending |

The requested PWM input target is 1–10 kHz. GPIO interrupt timestamps have not
been calibrated; displayed precision does not imply measurement accuracy.
Output logic levels and the 1.5-second master lease require physical
verification. This prototype is not a certified PLC or safety controller.

## 4. Reproduction

Build the fixed firmware projects from the repository root with ESP-IDF 5.5 or
newer:

```sh
idf.py -C MASTER build
idf.py -C DO4 build
idf.py -C DI4 build
```

Build the customer ESP-IDF example after resolving its GitHub component
dependencies, and validate/compile the ESPHome YAML with the product's selected
ESPHome release. Record the tool versions, source revision, board wiring and
observed results before changing a pending hardware or framework result to
Pass.
