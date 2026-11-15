# Placeholders and TBD values

Every value in the Dyno firmware that is a stand-in until we get the real number, a team decision, or a hardware measurement.

**Keep this file current.** When you add a placeholder or TBD value, add a row here in the same commit. When you replace one with the real value, move its row to "Resolved" at the bottom with the date and where the value came from.

Line numbers were last checked on 2026-11-15 (branch `feature/dyno-code-T6`). If a line has moved, search the file for the name.

## 1. Blocking: the firmware deliberately won't produce real output until these are filled in

These are guarded by a "confirmed" flag. While the flag is off, the output that depends on it is marked invalid and a `# WARNING` line prints on serial. Nothing breaks, but you won't get real torque or power.

| What | File:line | Current value | What we need | What it's for |
|---|---|---|---|---|
| `DYNO_PINS_CONFIRMED` | `include/Defines/pindefs.h:10` | `0` | Set to `1` after checking all five pins below against the GRC27 Dyno board schematic | Turns off the "pins unconfirmed" warning. Only flip it once every pin below is checked. |
| `PIN_HX711_DOUT` | `include/Defines/pindefs.h:12` | `16` (copied from GRC26) | GPIO number for the HX711 data pin on the GRC27 board (from the electrical team / schematic) | Reading the load cell amplifier |
| `PIN_HX711_SCK` | `include/Defines/pindefs.h:13` | `15` (copied from GRC26) | GPIO number for the HX711 clock pin on the GRC27 board | Reading the load cell amplifier |
| `PIN_CAN_TX` | `include/Defines/pindefs.h:14` | `40` (copied from GRC26) | GPIO wired to the CAN transceiver's TXD pin on the GRC27 board | Talking on the ECU CAN bus |
| `PIN_CAN_RX` | `include/Defines/pindefs.h:15` | `41` (copied from GRC26) | GPIO wired to the CAN transceiver's RXD pin on the GRC27 board | Receiving ECU data (RPM, lambda, temps, …) |
| `PIN_TARE_BUTTON` | `include/Defines/pindefs.h:21` | `0` (the devkit's BOOT button, for bench testing) | The tare-button GPIO on the proto board. Avoid strapping pins (0, 3, 45, 46): GPIO0 held at power-up stops the firmware running. `test_config` rejects pins that can't work. | The operator's tare button (#36). Not blocking: typing `tare` does the same. |
| `TARE_BUTTON_ACTIVE_LOW` | `include/Defines/pindefs.h:22` | `true` (button to GND, internal pull-up) | How the button is wired on the proto board (`false` = to 3.3 V, internal pull-down) | Whether a press reads LOW or HIGH. Backwards = the button tares on release. |
| `GEOMETRY_CONFIRMED` | `include/Defines/DynoConfig.h:48` | `false` | Set to `true` once `EFFECTIVE_ARM_M` is measured and the force direction is known (mechanical team) | While `false`: no torque and no power |
| `EFFECTIVE_ARM_M` | `include/Defines/DynoConfig.h:49` | `0.0` | The **perpendicular** distance from the shaft's centre to the load cell's line of force, **in metres** (mechanical team). Must be > 0 and ≤ 2 m or the build fails. | Torque = force × arm. Without it there is no torque. |
| `SAME_SHAFT_CONFIRMED` | `include/Defines/DynoConfig.h:50` | `false` | Confirmation that the load cell measures the engine crank (no belt/gearbox in between), checked with a tachometer against the CSV `rpm` column | While `false`: no power (torque can still be valid). Power uses ECU engine RPM, which is only right if it's the same shaft. |

How to fill these in: pins → `docs/loadcell-acquisition.md` "Hardware checklist" step 1 and `docs/can-decoding.md`. Geometry and same-shaft → the "Setup note: ask the mechanical team" table in `docs/torque-power.md` (around line 152) and "When the answers arrive" just below it.

## 2. Team decisions still to be made

These have a working value now; the team just hasn't agreed on the final one.

| What | File:line | Current value | What we need | What it's for |
|---|---|---|---|---|
| `CSV_ROW_INTERVAL_MS` | `include/Defines/DynoConfig.h:125` | `100` ms (10 rows/s) | The team to choose how many CSV rows per second they want. Allowed range 20–1000 ms (the build refuses anything else, or anything the serial speed can't carry). | How often a data row is printed over serial for logging. See `docs/csv-output.md:16`. |
| Accepted calibration error | `docs/calibration.md`, "Hardware checklist (#35)" step 5 | none set | The team to choose how far a calibrated reading may be from a known load (e.g. ±1% or ±0.5 N) before the calibration counts as failed | Pass/fail line for the expected-vs-measured table |

## 3. First guesses to retune at the first hardware session

These are reasonable defaults that already work. They aren't wrong, but they were chosen without real hardware, so check them once we can measure.

| What | File:line | Current value | What we need | What it's for |
|---|---|---|---|---|
| `CAN_FRESHNESS_MS` | `include/Defines/DynoConfig.h:26` | `2000` ms | The real send rate of each ECU message (read the `hz=` field in the `# can` status line). If the slowest message is slower than 1 every 2 s, raise this to ~5× its period. Table in `docs/can-decoding.md:101`. | How old an ECU value can be before it's treated as stale (blanked in the CSV). Assumes every message comes at ≥ 10 Hz, which hasn't been measured. |
| `FORCE_FRESHNESS_MS` | `include/Defines/DynoConfig.h:54` | `500` ms | Real HX711 sample rate and noise. If changed, update the boundary rows in `lib/DynoCore/src/TorquePowerExamples.h` (`test_config` fails until they match). | How old a load-cell reading can be before torque/power stop using it |
| `MAX_SOURCE_SKEW_MS` | `include/Defines/DynoConfig.h:55` | `250` ms | Real ECU and load-cell rates. Same `TorquePowerExamples.h` rule as above. | Max time gap between the force and RPM readings that get multiplied into power |
| HX711 sample rate (10 Hz vs 80 Hz) | not a setting; depends on how the HX711 RATE pin is wired on the board | code handles both | Measure it: `samples` in the `# loadcell` line should rise by ~50 (10 Hz) or ~400 (80 Hz) every 5 s. Record it in `docs/loadcell-acquisition.md:108`. | Confirms the assumptions behind `HX711_READY_DEADLINE_MS` and the freshness limits above |
| `HX711_READY_DEADLINE_MS`, `HX711_RETRY_LIMIT`, `HX711_RECOVERY_INTERVAL_MS`, `HX711_POLL_INTERVAL_MS` | `include/Defines/DynoConfig.h:13-16` | `150`, `3`, `1000`, `5` ms | Check on hardware that there are no false timeouts and that unplug/replug recovers as described in `docs/loadcell-acquisition.md` checklist steps 6–7 | How the load-cell task waits for, retries and recovers the HX711 |
| `CAL_WINDOW_MS`, `CAL_MIN_SAMPLES` | `include/Defines/DynoConfig.h:76-77` | `1000` ms, `8` | Real HX711 sample rate and noise. At 80 Hz, `CAL_MIN_SAMPLES` can go up (e.g. 40). If changed, update `lib/DynoCore/src/CalibrationExamples.h` (`test_config` fails until they match). | How much data is averaged for a calibration or tare reading (`docs/calibration.md`, "Stability rule") |
| `CAL_MAX_SPREAD_COUNTS` | `include/Defines/DynoConfig.h:78` | `500` counts | ~3× the measured unloaded max − min with the load cell mounted (`docs/calibration.md` checklist step 3). Same fixture rule. | When a reading counts as settled. Too low: calibration always `Unstable`; too high: accepts a swinging weight |
| `CAL_TIMEOUT_MS` | `include/Defines/DynoConfig.h:79` | `10000` ms | Check it's long enough to hang the weight and let it settle on the real rig. Same fixture rule. | When a calibration/tare step gives up |
| `CAL_MIN_SEPARATION_COUNTS` | `include/Defines/DynoConfig.h:81` | `5000` counts | Well under (reference load in N × measured counts/N). Needs the real load cell and the reference weight we'll use. Same fixture rule. | Rejects a calibration where the weight barely changed the reading |
| `TARE_DEBOUNCE_MS` | `include/Defines/DynoConfig.h:108` | `50` ms | The real button's bounce, measured by `test_device_tare_button` (`docs/calibration.md` checklist step 13). Must stay 40–150 ms (build check). If changed, update `lib/DynoCore/src/TareButtonExamples.h` (`test_config` fails until they match). | How long a press must last to count |
| `LOADCELL_SATURATION_COUNTS` | `include/Defines/DynoConfig.h:84` | `8388000` | Nothing to measure: it's near the HX711's ±8,388,607 limit. Check on hardware that unplugging the bridge wires trips it (`docs/calibration.md` checklist step 8d). Same fixture rule. | Rejects readings where the amplifier is maxed out |

## 4. Waiting on hardware or another ticket (not a number)

| What | File:line | What we need | What it's for |
|---|---|---|---|
| Fixture-only sample row | `docs/csv-sample.csv:2` and the `2600` row (`docs/csv-output.md:96`) | Replace with a real capture once the load cell is calibrated on the rig and the real arm is known (`test_csv_sample` checks the file byte for byte, so update the test's fixture too) | The example uses a made-up 100 N force and 0.25 m arm, labelled as fixture. Not real data. |

## 5. Planned: not in the code yet (from `DYNO_PLAN.md`)

Nothing planned right now. When a ticket plans a new placeholder, list it here until it is in the code.

## Resolved

| What | Was | Now | Date | Source |
|---|---|---|---|---|
| Calibrated force (#35 waiting row) | force always "unavailable" in `CsvTask.cpp`; `# WARNING: no calibrated force yet` | `CsvTask` peeks #35's force mailbox; the warning only shows while not calibrated | 2026-11-15 | #35 (`docs/calibration.md`) |
| `CAN_BITRATE_KBPS` (`DynoConfig.h:25`) | — | `500` kbit/s | before 2026-11-15 | GRC26 Dyno + GRC26 `TELEMETRY.md` (same MaxxECU) |
