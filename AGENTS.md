# MECS project notes

MECS is a modular I/O system: fixed ESP-IDF firmware runs on expansion nodes,
while the customer's master firmware uses a MECS client library to discover
nodes and operate their pins without dealing with CAN frames. The web UI and
serial console in `MASTER/` are development tools; keep them out of the public
client library.

## Where code belongs

- `MASTER/`: ESP-IDF test master, board-specific CAN pins, Wi-Fi dashboard and
  serial commands.
- `DI4/`: four-input node application, its pin map and GPIO/PWM measurement.
- `DO4/`: four-output node application, its pin map and GPIO/PWM generation.
- `SHARED/components/mecs_core/`: portable protocol and I/O behavior shared by
  the master and nodes.
- `SHARED/components/mecs_client/`: portable C client and managed C++ node/pin
  API used by customer master firmware.
- `SHARED/components/mecs_espidf/`: ESP-IDF CAN transport. Framework adapters
  live under `SHARED/adapters/`.
- `examples/`: customer master examples for ESP-IDF, Arduino IDE and ESPHome.
- `SHARED/tests/`: host tests for portable code; `SHARED/docs/`: manuals.

Keep board identity, local GPIO assignments, and board-only hardware behavior
inside that board's folder. Put code in `SHARED/components/` only when it is
reused or is part of the customer-facing library. Framework adapters should
call the shared API; do not copy protocol handling into examples.

## Making changes

Follow a request from the public API down to the portable model, then the
board-specific hardware callback. Keep functions small and names descriptive.
Document public settings and non-obvious timing/safety choices where they are
defined. Preserve the node's inactive output behavior after master loss.

When changing protocol properties or behavior, update the portable validation
and wire codec, affected node hardware, master/client API, framework examples,
manuals and relevant host tests together. Build packages from canonical
`SHARED/` sources with the packager scripts instead of editing generated copies.

Run the portable checks after client or protocol changes:

```sh
cmake -S SHARED/tests -B /tmp/mecs-build
cmake --build /tmp/mecs-build --parallel
ctest --test-dir /tmp/mecs-build --output-on-failure
```

Build firmware from the repository root with ESP-IDF 5.5 or newer:

```sh
idf.py -C MASTER build
idf.py -C DO4 build
idf.py -C DI4 build
```

See `SHARED/docs/DEVELOPMENT.md`, `SHARED/docs/MECS_API.md` and
`SHARED/docs/VERIFICATION.md` for detailed architecture, API and acceptance
guidance.
