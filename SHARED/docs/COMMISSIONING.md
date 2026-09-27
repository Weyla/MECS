# Installation and operation manual

| Document control | Value |
|---|---|
| Document ID | MECS-INST-001 |
| Revision / date | 3.1 / 2026-09-27 |
| Firmware / wire | 0.3.1 / 3 |
| Hardware | ESP32-C3 SuperMini with SN65HVD230 transceivers |

## 1. Wiring and board roles

| Connection | Master | Output board 1 | Input board 2 |
|---|---|---|---|
| CAN TX → transceiver TXD | GPIO4 | GPIO4 | GPIO4 |
| CAN RX ← transceiver RXD | GPIO5 | GPIO5 | GPIO5 |
| Channels 0,1,2,3 | — | GPIO0,1,2,3 | GPIO0,1,2,3 |
| Logical node address | 0 | 1 | 2 |

Connect transceiver CANH to CANH, CANL to CANL, and share ground. Use the module's
normal operating mode, 3.3 V-compatible logic and appropriate supply. Keep two
120-ohm terminations at the physical ends; the previously reported unpowered
CANH–CANL resistance is 60 ohms. Do not change existing CAN wiring for this update.

GPIO2 is a boot strapping pin. GPIO0–3 are the user's selected channel mapping;
connected circuitry must allow correct reset behavior. Firmware cannot guarantee
pin levels before it runs. Use external bias appropriate to the load if a defined
reset level is required. Outputs default active-high/inactive-low. If drivers are
active-low at power-up, set the output build's **Boot-time output polarity** to
active-low before flashing. Runtime polarity changes do not survive reboot.

Outputs are **3.3 V logic signals**. Relays, motors, high-current LEDs and field
loads need suitable external drivers/protection. Inputs are digital, not ADC
channels. A simple input test uses a switch between a channel GPIO and ground
with the default pull-up/active-low settings. No 5 V or 24 V field signal may be
connected directly to an ESP32 GPIO.

## 2. Build and flash

Use ESP-IDF 5.5 or newer and target `esp32c3`. The firmware CI builds with
ESP-IDF 5.5. To activate a locally installed 6.1 toolchain on this workstation:

```sh
source /home/renato/.espressif/tools/activate_idf_v6.1.sh
cd /home/renato/MECS/Software
```

**Revision 3 is incompatible with older firmware: update every participating board.**

Build the three separate images:

```sh
idf.py -C MASTER build
idf.py -C DO4 build
idf.py -C DI4 build
```

Each board project has its own main.c, board pin assignments and sdkconfig, and calls shared libraries from SHARED. For a fresh project, `idf.py -C <project> set-target esp32c3` can
initialize the target before building. Do not rerun `set-target` routinely: it
regenerates configuration. Existing `sdkconfig` overrides initial defaults.
Check `menuconfig → MECS DO4 output node` before repurposing an old build directory.

| Project | Address | Profile and pins |
|---|---:|---|
| `MASTER` | 0 | CAN GPIO4/5; native USB console; main task stack 8192 |
| `DO4` | 1 | Output GPIO0–3; choose boot polarity to match drivers |
| `DI4` | 2 | Input GPIO0–3 |

Identify serial ports by USB device and board label, not numbering alone.
USB enumeration can change after reset. Identify each board by the USB device
entry or MAC before choosing a port; do not assume ttyACM numbering stays fixed.

```sh
idf.py -C MASTER -p PORT flash monitor
idf.py -C DO4 -p PORT flash monitor
# Connect and identify the input board before flashing it:
idf.py -C DI4 -p PORT flash monitor
```

Only one program may own a serial port at a time. Close CuteCom before flashing
or opening IDF Monitor. Exit IDF Monitor with Ctrl+]. CuteCom settings are 115200,
8 data bits, no parity, one stop bit, no flow control, LF line ending.

## 3. Connect the master to Wi-Fi

From the **master's USB serial console**, enter one line:

```text
wifi Your network name|Your password
```

The SSID may contain spaces; the first `|` separates it from the password.
SSID length is 1–32 bytes. Password is 8–63 bytes, or empty for an open test
network. This console accepts printable ASCII. The `|` character is therefore
not supported inside an SSID; it is allowed after the separator in a password.
Credentials are stored in ESP-IDF NVS and reused after reset. They are not printed
by the firmware or accepted through the web console. A terminal's own input
history may retain text, so handle that history appropriately.

Connect to a 2.4 GHz network. The master prints:

```text
Web interface: http://192.168.x.x/
```

Open that exact address on the same LAN. There is no automatic access-point
fallback, captive portal or Internet dependency. DHCP may change the address;
check serial after reconnecting or rebooting. The UI is served entirely from the
master firmware. If no credentials are saved, CAN continues working and serial
prints the provisioning syntax.

The web controller has no user login or TLS in this prototype. It is intended
for a trusted development LAN. A same-origin request token protects browser
commands against ordinary cross-origin submissions; it is not user authentication.

## 4. Use the dashboard

The top row shows online/known nodes, CAN bitrate, master uptime and queued or
outstanding commands. **Discover nodes** refreshes discovery. Each card shows
node type, firmware, communication age and fault state.

Every editable field has an **Apply** button and a separate **Confirmed** value.
Changing an input field does not alter the device until Apply is selected.
An HTTP accepted response means queued; inspect confirmed readback and the event
log for the node's result. Use **Refresh settings** after uncertainty or timeout.
Settings are refreshed automatically when a node appears or reboots.

Input cards show raw HIGH/LOW and filtered ACTIVE/INACTIVE separately. Output
cards show instantaneous pin level and commanded gate. A PWM-enabled gate can
remain ACTIVE even with 0% duty; this means the gate is enabled, not that current
is flowing. The PWM period shown is computed from the configured frequency; it
is not a measured waveform. Duty is logical active time in percent, with up to two decimal places.

A node becomes stale after 1.5 seconds without status. The dashboard disables
its controls and stops presenting its signals as live. If the browser loses the
master, all node controls are disabled. Closing the browser does **not** stop a
healthy master's heartbeat; outputs keep their commanded state until explicitly
changed or the master heartbeat is lost.

## 5. Input processing

| Mode | Behavior | Useful for |
|---|---|---|
| None | Polarity-adjusted sampled pin state | Clean digital signals |
| Stable debounce | Accept a new state only after it remains unchanged for `filter_ms` | Mechanical contacts |
| PWM measurement | Timestamp rising/falling edges; report frequency, period and active duty | Repetitive digital PWM |

Digital sampling is nominally once per millisecond. Stable debounce requires a
continuous unchanged level for the configured duration. PWM capture runs from
GPIO interrupts independently of sampling. Reconfiguration clears old filter
and capture history.

For PWM, choose **PWM measurement** and the signal's active polarity. The target
range is 1 Hz–10 kHz on this prototype. All four channels support the selection;
simultaneous upper-range operation is not yet physically characterized. Signals
must be 3.3 V-compatible with a shared ground. The display reports the latest
complete cycle. Active-low duty measures the LOW portion; active-high measures
HIGH. It is not an average or an edge recorder.

A missing/invalid signal displays **no valid signal / stale**. A constant LOW or
HIGH cannot provide a period, so 0%/100% DC is invalid for measurement. Accepted
high and low pulses must each be at least 4 µs; this is a software validity limit,
not guaranteed accuracy. Missing signal timeout is max(100 ms, three measured
periods), followed by reporting delay. Interrupt overload temporarily suspends
capture and retries. Microsecond timestamp units and decimal display precision
do not guarantee that physical accuracy. Validate against a signal generator
or scope before relying on a measurement; see the verification record.

`pull` selects floating, pull-up or pull-down. Default is pull-up and active-low.
Reporting is independent from filtering: choose periodic, on change, or both.
Change reports are limited to 20 ms minimum spacing; periodic interval is
20–500 ms; every mode includes a status heartbeat at least every 500 ms.

Example through either console:

```text
set 2 0 polarity 1
set 2 0 pull 1
set 2 0 filter 1
set 2 0 filter_ms 20
get 2 0 filter_ms
```

## 6. Output operation

Outputs boot inactive. Each channel has independent **Enable** and **Inactive**
controls. Settings can be changed while the channel is
running. Each successful write applies immediately without clearing its enable
gate. Multiple property writes are sequential, not an atomic update.

Digital example:

```text
set 1 0 mode 0
set 1 0 polarity 0
on 1 0
off 1 0
```

Hardware PWM example, including a live duty edit:

```text
set 1 0 mode 1
set 1 0 frequency 1000
set 1 0 duty 25.50
on 1 0
set 1 0 duty 32.75
```

Slow PWM example, 60.5-second period and 25% active time:

```text
set 1 0 mode 3
set 1 0 period 60.5
set 1 0 duty 25
on 1 0
```

Slow periods range from 0.1 to 3600 seconds in 0.1-second steps. Both PWM modes
share duty (0–100%, steps 0.01%). Slow PWM uses a nominal 1 ms task, so very short
active portions are rounded down to milliseconds and may disappear. It is for
slow switching, not precision waveform generation. Enabling a stopped channel,
changing mode, or writing period begins a new cycle. A duty edit preserves phase
and immediately applies the new threshold; it may change the pin immediately.
An enable command on an already enabled channel does not restart its cycle.

Timed pulse example:

```text
set 1 0 mode 2
set 1 0 pulse_ms 100
pulse 1 0
```

Use **Pulse**, not Enable, in timed pulse mode. A new trigger restarts the pulse;
a duplicate transaction does not. Changing pulse duration while active starts
the new duration from that edit. Entering pulse mode from an enabled mode starts
a pulse immediately. Inactive cancels it. Termination happens locally.

Wait for confirmed results before a dependent action. Hardware PWM channels have
independent timers. Duty edits within a running PWM use the LEDC duty update API.
Frequency, polarity and mode changes can reconfigure the pin/peripheral; their
transitions are not guaranteed glitch-free. Active-low inverts the electrical
waveform while preserving logical active duty. Duty 0%/100% becomes a constant
GPIO level. Requested frequency/duty are quantized by the hardware; configured
readback is not measured feedback.

After 1.5 seconds without a valid master heartbeat, output gates clear and pins
return to their configured inactive levels. Reconnection requires new individual
activation commands. A master reboot changes session and also clears gates.
Hardware application errors latch a fault and attempt to stop all outputs;
correct the cause and reboot the node. These are software behavior, not a
substitute for hardware interlocks.

## 7. Command reference

| Command | Action |
|---|---|
| `help` | Print grammar |
| `discover` | Request announcements |
| `nodes` | Print online status and pin bits |
| `refresh N` | Read every supported setting from node N |
| `get N CH PROPERTY` | Read a named property |
| `set N CH PROPERTY VALUE` | Write a named property with range checking |
| `on N CH` / `off N CH` | Output gate |
| `pulse N CH` | Trigger a configured timed pulse |
| `wifi SSID\|PASSWORD` | USB-only Wi-Fi provisioning |

Channel numbers are 0–3. Use channel 255 for `report`, `report_ms` when
using get/set syntax. See [PROTOCOL.md](PROTOCOL.md) for the complete property
names, enums, limits and error meanings. Numeric tokens are unsigned decimal. Only duty and period accept fractional
values; excess decimal places, exponents and negative values are rejected.

## 8. Troubleshooting

| Observation | Action |
|---|---|
| No nodes | Verify matching revision-3 firmware, unique addresses, power, ground, TX/RX and termination |
| Wrong node type | Check which board project was built and flashed |
| Settings remain unknown | Wait for readback; inspect errors; request `refresh N` |
| Output configuration rejected | Check supported property, range, heartbeat and fault flag |
| Output action rejected | Check mode, live master heartbeat and fault flag |
| Transaction timeout | Outcome is unknown; refresh readback/status before retrying a pulse |
| Lost node stays listed | Expected: historical entries remain but are marked stale |
| No web address | Provision credentials over USB; check 2.4 GHz Wi-Fi and DHCP |
| Web page unreachable | Same LAN, no client isolation, correct current DHCP address; check serial |
| CAN warning/passive/bus-off | Check peer, wiring and bitrate; recovery does not repair electrical faults |
| RX overflow | Remove unrelated bus traffic; repeat discovery/readback |
| Pins briefly change on reset | Match external bias and build-time polarity; firmware starts after reset hardware |

All runtime channel settings are lost on node reset. Wi-Fi settings and master
session counter persist in NVS. Flashing application images normally leaves NVS
intact; erasing flash removes it.
