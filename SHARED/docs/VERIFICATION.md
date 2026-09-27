# Verification record

| Document control | Value |
|---|---|
| Document ID | MIO-TEST-001 |
| Revision / date | 3.1 / 2026-09-27 |
| Software / wire | 0.3.0 / 3 |
| Hardware roles | Master; node 1 DO4; node 2 DI4 |

## 1. Build verification

The separate projects now live in `MASTER`, `DO4` and `DI4`. Each project
selects its components from `SHARED/components`.

| Project | Configuration | Result |
|---|---|---|
| MASTER | ESP-IDF 6.1, ESP32-C3 | Pass |
| DO4 | ESP-IDF 6.1, ESP32-C3 | Pass |
| DI4 | ESP-IDF 6.1, ESP32-C3 | Pass |

All images linked and fit the configured application partition. These checks
verify compilation and linking; they do not establish electrical behavior or
timing accuracy.

## 2. Existing software checks

Before the folder reorganization, the host model and protocol checks passed on
the earlier `modular_io/tests` tree. Shared source was copied into SHARED/components. DI4's edge-to-period estimator
now resides in DI4/main because only that input board uses it; the shared
measurement wire type and codec remain common to DI4 and MASTER. The host tests have not yet been moved to or run against
the new folder tree, so the earlier result is historical evidence only.

The dashboard's JavaScript and simulated desktop/mobile flows passed before it
was copied into `MASTER`. Those checks cover presentation and command entry;
they do not exercise the ESP32 HTTP server, CAN or physical pins.

## 3. Hardware acceptance

No board has been flashed from the new `MASTER`, `DO4` or `DI4` projects yet.
The new firmware has therefore not been checked on the bus. Flashing and bench
results must be entered here after testing.

| ID | Check | Result |
|---|---|---|
| H01 | Flash revision 3 master and node firmware | Pending |
| H02 | Master discovers DO4 at address 1 and DI4 at address 2 | Pending |
| H03 | Confirm property reads/writes and decimal PWM duty | Pending |
| H04 | Change an enabled output value without disabling its gate | Pending |
| H05 | Measure input PWM frequency and duty against a signal generator | Pending |
| H06 | Check all four input capture channels and upper test frequency | Pending |
| H07 | Confirm each output returns inactive after master heartbeat loss | Pending |
| H08 | Verify slow PWM period and duty with a logic analyzer | Pending |
| H09 | Confirm polarity, GPIO readings, Wi-Fi and dashboard on the new master | Pending |

The requested PWM input target is 1–10 kHz. GPIO interrupt timestamps have not
been calibrated; displayed precision does not imply measurement accuracy.
Output logic levels and 1.5-second master lease also need physical verification.
The prototype is not a certified PLC or safety controller.

## 4. Reproduction

From the workspace root, activate ESP-IDF 6.1 and build:

```sh
idf.py -C MASTER build
idf.py -C DO4 build
idf.py -C DI4 build
```

Do not mark hardware checks complete from a successful build or simulated UI run.
