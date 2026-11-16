# Bench test: integration, failure matrix and results (#39)

This is the checklist for proving the whole Dyno firmware works together. Each part was built and tested on its own in #31–#38; this ticket joins them up. Another member should be able to follow it without reading the code.

It has two halves:

1. **Synthetic (done, no hardware).** The real decoder, calibrator, tare button logic, torque/power maths and CSV formatter are wired together on the host and fed scripted inputs with a fake clock. Every result from this half is labelled **SYNTHETIC**. It proves the pieces fit and that the failure rules hold. It does not prove the wiring, the ECU, the load cell or the mechanics.
2. **Hardware (blocked until the board, load cell and ECU are on the bench).** An ordered run sheet. A step that can't be done because something is missing is marked **blocked**, never passed.

How to run the dyno day to day is in [operation.md](operation.md). This file is about checking it.

## 1. Inventory

What is being tested. Fill in the "tested revision" column with the commit hash on the day of each run.

| Part | Ticket | PR | What it provides | Tested revision |
|---|---|---|---|---|
| ESP config + RTOS foundation | #31 | #52 (merged) | Board, tasks, queues | ______ |
| HX711 acquisition | #34 | #58 | `LoadCellAcquirer`, `Hx711Provider`, `LoadCellTask` | ______ |
| ECU CAN decoding | #33 | #58 | `EcuDecoder`, `CanReceiver`, `TwaiCanBus`, `CanTask` | ______ |
| Torque and power | #37 | #58 | `computeTorquePower` | ______ |
| Combined serial CSV | #38 | #59 | `CsvPublisher`, `CsvTask`, `OutputTask`, `tools/check_csv.py` | ______ |
| Calibration and storage | #35 | #61 | `LoadCellCalibrator`, NVS store, serial commands | ______ |
| Tare button | #36 | #62 | `TareButton`, `CommandTask` | ______ |
| Integration (this) | #39 | ______ | `test_integration`, `docs/synthetic-run.csv`, this file, [operation.md](operation.md) | ______ |

Shared settings, as they are in the code today:

| What | Value | Where |
|---|---|---|
| Board | `esp32-s3-devkitc-1` (envs `esp32-s3-devboard`, `esp32-s3-fake`) | `platformio.ini` |
| Serial | 115200 baud, lines end in CRLF | `DYNO_SERIAL_BAUD`, [csv-output.md](csv-output.md) |
| CAN | 500 kbit/s, 13 signals from 7 messages (0x520, 0x521, 0x522, 0x526, 0x527, 0x530, 0x536) | `CAN_BITRATE_KBPS`, `lib/DynoCore/src/EcuSignals.h` |
| DBC revision | `7faab7e` | [can-decoding.md](can-decoding.md) |
| Freshness | CAN 2000 ms, load cell / force 500 ms, force-to-RPM gap for power 250 ms | `DynoConfig.h` |
| Calibration | 1000 ms window, ≥ 8 samples, spread ≤ 500 counts, 10 s timeout, ≥ 5000 counts separation; saved to NVS. A tare is **not** saved. | `DynoConfig.h`, [calibration.md](calibration.md) |
| Tare button | GPIO0 (devkit BOOT) placeholder, active low, 50 ms debounce | `pindefs.h`, `DynoConfig.h` |
| Geometry | **Placeholder**: arm 0.0 m, `GEOMETRY_CONFIRMED=false`, `SAME_SHAFT_CONFIRMED=false`, so torque and power are blank on the real firmware | `DynoConfig.h`, [torque-power.md](torque-power.md) |
| CSV schema | version 1, 27 columns, header printed once at boot | `CsvSchema.h`, [csv-output.md](csv-output.md) |
| Row rate | 100 ms (10 rows/s), output queue 16 lines | `DynoConfig.h` |

Everything still unknown (pins, geometry, tuning) is listed in [PLACEHOLDERS.md](../PLACEHOLDERS.md).

### Test commands

Run from `GRC-27/Dyno/`. The host commands need no hardware.

| Command | Covers | Owner ticket | Hardware |
|---|---|---|---|
| `pio test -e native` | Everything below at once (264 tests) | all | none |
| `pio test -e native -f test_loadcell` | HX711 timeouts, retries, fault, recovery (16) | #34 | none |
| `pio test -e native -f test_can` / `-f test_can_receiver` | Every signal's decode, freshness, driver start/retry/bus-off (31 + 21) | #33 | none |
| `pio test -e native -f test_torque_power` | Formulas, units, validity, skew, sign (41) | #37 | none |
| `pio test -e native -f test_csv` / `test_csv_publisher` / `test_csv_pipeline` / `test_csv_sample` | Columns, blanks, drops, whole pipeline with fake ECU, published sample (25 + 9 + 4 + 2) | #38 | none |
| `pio test -e native -f test_calibration` / `test_calibrator` / `test_stable_window` / `test_calibration_pipeline` / `test_command` | Stored record, calibrator, stability rule, commands (14 + 30 + 17 + 2 + 9) | #35 | none |
| `pio test -e native -f test_button` / `test_tare_button` | Debounce, one press = one tare (11 + 11) | #36 | none |
| `pio test -e native -f test_config` | The settings in `DynoConfig.h` are self-consistent (11) | all | none |
| `pio test -e native -f test_integration` | **This ticket**: the whole rig joined, failure matrix, restart, synthetic run (10) | #39 | none |
| `python3 tools/test_check_csv.py` | The capture checker, the sample and the synthetic run (13) | #38, #39 | none |
| `pio run -e esp32-s3-devboard` / `pio run -e esp32-s3-fake` | Firmware builds | #31 | none |
| `pio test -e esp32-s3-devboard -f test_device_can_loopback` | Real CAN driver, jumper TX→RX (6) | #33 | devkit + jumper |
| `pio test -e esp32-s3-devboard -f test_device_tare_button` | Real button pin and bounce (3, interactive) | #36 | devkit |

No feature owner left a command unimplemented. The one gap: **the task files in `src/Tasks/` are not run on the host.** See "What the synthetic half does not prove" below.

## 2. The synthetic rig

`test/test_integration/test_integration.cpp` builds the whole dyno out of the real `lib/DynoCore` parts:

```
FakeCanBus ─► CanReceiver ─► ECU mailbox ──────────┐
FakeLoadCellProvider ─► LoadCellAcquirer ─► sample ┤
typed commands ─┐                                  ├─► CsvPublisher ─► 16-line queue ─► "PC" (prints 1 line every N ms, or stalls)
FakeButton ─► TareButton ┴─► LoadCellCalibrator ─► force ┘           ▲
                                                 "# " replies ───────┘
```

It is wired the same way `TaskFactory` wires the real tasks (same order of steps, same periods: load cell every 5 ms, commands every 20 ms, a row every 100 ms), driven by a fake clock in 5 ms ticks. The tests read only what an operator would see: the printed CSV rows and `# ` lines, plus the public counters.

### Fixture values (FIXTURE ONLY, not the real dyno)

- Calibration: unloaded reading 100000 counts, 100 N reference reads 120000 counts → 200 counts/N.
- Geometry: 0.25 m arm, same shaft confirmed. The real firmware keeps torque/power blank until the mechanical team gives the real arm.
- ECU: all 7 messages at 50 Hz, RPM = 1000 + t/2 (t in ms).

### What the synthetic half does not prove

- **The task files themselves** (`src/Tasks/*.cpp`, `TaskFactory`) only compile on the host; the rig copies their wiring in test code. They are checked on a devkit with the fake build (run sheet step H1). Until then, a wiring mistake inside a task file would not be caught.
- Wiring, pins, the ECU's real IDs/rates, the HX711's real rate and noise, the real button's bounce, NVS on a real chip, USB serial timing, the arm and the shaft.

## 3. Expected results: failure matrix

One row per scenario from the ticket. "Synthetic" is the host test that proves it. "Hardware" is the run sheet step. Fill in **Runner** and **Reviewer** (a second member checks the result) on the day.

| # | Scenario | Expected (values, validity, timing) | Synthetic test (`test_integration`) | Synthetic | Hardware step | Hardware result | Runner | Reviewer |
|---|---|---|---|---|---|---|---|---|
| 1 | All CAN inputs, valid calibrated load | All 27 cells filled. Fixture: rpm 7000, lambda 1.000/0.990/1.010, force 50.000 N, torque 12.500 N·m, power 9163.0 W, 12.288 hp, cal v1, tare v1, masks 127/127 | `test_all_inputs_valid_and_calibrated_gives_the_worked_row` | **Pass** | H6, H7 | blocked | | |
| 2 | Some ECU messages never arrive (0x530, 0x536) | Masks 31/31. Battery, IAT, coolant, gear blank (never 0). RPM and power keep updating | `test_missing_ecu_messages_stay_blank_while_the_rest_update` | **Pass** | H2 step 4 (ECU config), H7 | blocked | | |
| 3 | ECU/CAN stops | Load cell keeps updating (force 50 → 60 N while CAN is silent). Power blank once RPM is > 250 ms older than the force. ECU cells blank once 2000 ms old (fresh mask 0, received stays 127). Back within one row of CAN returning. No missing rows. | `test_ecu_stops_load_cell_carries_on_and_power_goes_invalid` | **Pass** | H7 a | blocked | | |
| 4 | HX711 unavailable | `loadcell_valid=0`, raw/force/torque/power blank; RPM keeps counting; rows every 100 ms; no wait longer than 20 ms; recovers by itself with the saved calibration | `test_hx711_stops_can_carries_on_and_nothing_waits` | **Pass** | H3 steps 6–7, H7 b | blocked | | |
| 5 | Tare with readings still queued | Every row's force matches the zero of the `tare_version` printed on that same row. No old-zero row after a new-zero row. Drops counted. | `test_tare_never_mixes_a_queued_reading_with_the_new_zero` | **Pass** | H4 step 7, H7 c | blocked | | |
| 6 | Button bounce, repeated press, unstable tare | Bounce = no press. Press during a tare = `tare: busy`. Unstable = `tare FAILED`, old zero kept. Exactly one good tare (v1 → v2), calibration untouched, nothing written to flash | `test_bounce_repeat_and_unstable_tare_leave_one_clean_tare` | **Pass** | H4 step 13 | blocked | | |
| 7 | Zero and signed values | 0 N prints `0.000` (valid, not blank). −50 N prints `-50.000`, torque `-12.500`, power `-8508.5` (sign kept) | `test_zero_is_a_valid_value_and_negative_keeps_its_sign` | **Pass** | H5 (reverse load) | blocked | | |
| 8 | Slow then stalled PC, queue full | Queue never over 16. Every missing row counted in `output_dropped_rows` (gap = count increase, exactly). Load cell kept sampling at 10 Hz and a typed command was answered during a 10 s stall. Rows after the stall are current. | `test_stalled_pc_drops_counted_rows_while_everything_else_runs` | **Pass** | H7 d | blocked | | |
| 9 | Restart / reset | Header first (after `# ` startup lines). Calibration loaded (`cal: loaded the saved calibration`, v1). Tare **not** kept: tare version back to the saved one, zero back to the calibration's. Booting writes nothing to flash. Never-calibrated board says so and leaves force blank. | `test_restart_restores_the_calibration_but_not_the_tare` | **Pass** | H4 step 6–7, H7 e | blocked | | |
| — | Whole synthetic session | `docs/synthetic-run.csv` printed byte for byte; passes `check_csv.py` | `test_synthetic_run_matches_the_published_capture`, `tools/test_check_csv.py` | **Pass** | — | — | | |

Synthetic results: `pio test -e native`, 264/264 passing, on branch `feature/dyno-code-T7`, 2026-11-15.

### Do the synthetic tests actually catch bugs?

Each rule was broken on purpose in the real code, one at a time, to confirm a test fails (then put back):

| Deliberate bug | Caught by |
|---|---|
| Power ignores the 250 ms force/RPM gap | row 3, synthetic run |
| A tare doesn't bump `tare_version` | rows 5, 6, 9, synthetic run |
| Dropped rows aren't counted | rows 5, 8, synthetic run |
| A 0 value prints blank | rows 5, 6, 7, 9, synthetic run |
| A negative value loses its sign | row 7 |
| A tare is saved to flash | rows 6, 9, synthetic run |
| "Fresh" CAN means "ever received" | row 3, synthetic run |
| A load-cell reading's age is ignored | not by the rig (the acquirer already faults a silent HX711 after 150 ms, before the 500 ms age limit); caught by `test_csv`'s `test_loadcell_raw_fresh_until_age_reaches_limit` |

### The published synthetic run

[`synthetic-run.csv`](synthetic-run.csv) is the reproducible fake-input evidence: one 25 s session plus a reboot, with `# SYNTHETIC` labels saying what happens when. Calibrate at 1 s and 4 s → tare button at 10 s → ECU silent 14–17 s → load cell unplugged 18–20 s → PC stops reading 21–23 s (5 rows dropped and counted) → reboot with the same flash. Check it with:

```sh
python3 tools/check_csv.py docs/synthetic-run.csv
```

If a code change is meant to change the output, regenerate it and review the diff before committing:

```sh
DYNO_WRITE_SYNTHETIC_RUN=1 pio test -e native -f test_integration
git diff docs/synthetic-run.csv
```

## 4. Hardware run sheet (blocked)

Do these **in order**, one input at a time; don't move on until the step before passes or its problem is understood. Don't use a running engine as the first test of anything. Each step points to the detailed checklist written with that feature. Write results here or in the PR; mark anything that can't be done **blocked** with the reason.

Before starting, record:

| | |
|---|---|
| Date | ______ |
| Firmware commit | ______ |
| Board (which one, serial no.) | ______ |
| HX711 module / load cell model | ______ |
| Wiring reference (schematic rev) | ______ |
| Serial monitor + settings | ______ (115200, CRLF) |
| CAN source (ECU / CAN tool) and its settings | ______ |
| Continuous run length (choose **before** H7) | **10 minutes** |

| Step | What | Detailed checklist | Needs | Result (pass/fail/blocked + notes) |
|---|---|---|---|---|
| H1 | **Fake build on a bare devkit.** `pio run -e esp32-s3-fake -t upload && pio device monitor`. Header, `# FAKE INPUTS`, CSV masks follow the fake ECU timeline, `loadcell_raw` ramps then faults at 27 s. This is the first run of the real task files. Capture 2 min, `check_csv.py` passes. Also run `test_device_can_loopback` and `test_device_tare_button`. | [csv-output.md](csv-output.md) checklist 1–3, 6–9; [can-decoding.md](can-decoding.md) step 0; [calibration.md](calibration.md) step 13 (first bullet) | devkit | blocked |
| H2 | **CAN alone.** Real board, ECU (or a CAN tool sending the 7 IDs), no load cell. Pins, termination 60 Ω, ECU off/on, compare values with the ECU's own display, message rates, unplug/replug. Confirm only the 13 approved signals appear (no extra columns). | [can-decoding.md](can-decoding.md) checklist 1–7 | board, ECU or CAN tool | blocked |
| H3 | **HX711 alone.** Pins, wiring, unloaded noise, gentle load direction, sample rate, unplug/replug. | [loadcell-acquisition.md](loadcell-acquisition.md) checklist 1–7 | board, HX711, load cell | blocked |
| H4 | **Calibration and tare.** Calibrate with a known mass, check independent loads (expected vs measured table), reboot keeps calibration, tare not kept, failure cases, re-flash, kg vs N, repeatability, button. | [calibration.md](calibration.md) checklist 1–13 | H3 + reference masses | blocked |
| H5 | **Geometry and shaft.** Mechanical team's setup note, measure arm, same-shaft check with a tachometer, then set `EFFECTIVE_ARM_M`, `GEOMETRY_CONFIRMED`, `SAME_SHAFT_CONFIRMED`. Static torque check with a known mass (also the opposite direction: sign must be negative). Worked comparison below. | [torque-power.md](torque-power.md) checklist 1–3 | H4 + mechanical team | blocked |
| H6 | **All together.** CAN and load cell both connected, calibrated, engine off first. Then, only with the team's normal engine-run procedure, at idle: every cell of a row has a value. Engine off: RPM cells follow what the ECU sends; note what power shows: ______. | — | H2–H5 | blocked |
| H7 | **Failure matrix + 10-minute continuous run.** One run of 10 minutes, with the actions below at the times written down beforehand. Capture the whole thing (`pio device monitor -f log2file`), then `check_csv.py` it. See below. | [csv-output.md](csv-output.md) checklist 4–8 | H6 | blocked |
| H8 | **Second member walkthrough of [operation.md](operation.md).** Someone who didn't write it follows it from a blank board; every missing or wrong instruction is fixed. | — | H1 at least | blocked |

### H7: the 10-minute continuous run

The length is fixed before the run (10 min) so it can't be stretched until it passes. Do these, noting the board time (`timestamp_ms`) of each:

| Time (min) | Action | Expected | Seen |
|---|---|---|---|
| 0 | Reset board, start capture | Header, startup `# ` lines, rows at 10/s | ______ |
| 1 | `tare` (nothing hung) | `tare OK`, `tare_version` +1, force ≈ 0 | ______ |
| 2 | Hang reference mass | force ≈ reference, power valid if RPM fresh | ______ |
| 3 (a) | Unplug CAN for 10 s | ECU cells blank within 2 s, power blank within 0.25 s, force keeps updating; back within 1 s | ______ |
| 4 (b) | Unplug HX711 for 10 s | `loadcell_valid=0`, force/torque/power blank within 0.5 s, RPM keeps updating, `faults` +1; `recoveries` +1 after | ______ |
| 5 (c) | Press the tare button with the mass still hung, then remove the mass | One `tare OK`; each row's force agrees with its own `tare_version`; mass now reads negative | ______ |
| 6 (d) | Pause the monitor 10 s | Gap in `timestamp_ms` = increase in `output_dropped_rows`; `# loadcell samples` kept rising | ______ |
| 8 (e) | Press reset | Header again (check_csv counts a restart), calibration loaded, tare gone | ______ |
| 10 | Stop capture | | |

At the end record: `check_csv.py` result ______; from the last `# loadcell`, `# can`, `# output` lines: `timeouts` ______ `faults` ______ `recoveries` ______ `samples` ______ `missed` ______ `bus_errors` ______ `rows_dropped` ______ `diagnostics_dropped` ______ `queue_free` ______. Any unexpected reset ______.

Don't take "on the same row" as "measured at the same time": check that each source was fresh on its own (its mask bit or valid flag).

### Worked torque/power comparison (H5)

| | Value |
|---|---|
| Mass hung (kg) | ______ |
| Force = kg × 9.80665 (N) | ______ |
| Measured `force_n` (sign as printed) | ______ |
| Direction (engine push = positive?) | ______ |
| Arm, perpendicular (m) | ______ |
| Torque = force × arm (N·m) | ______ |
| Measured `torque_nm` | ______ |
| RPM from ECU / tachometer on the load-cell shaft | ______ / ______ |
| Power = torque × rpm × 2π / 60 (W) | ______ |
| Measured `power_w` | ______ |
| hp = W / 745.699872 (mechanical hp) | ______ |
| Measured `horsepower_hp` | ______ |

If the shaft or arm can't be confirmed, leave `GEOMETRY_CONFIRMED`/`SAME_SHAFT_CONFIRMED` false (torque/power stay blank) and mark this row **open**.

## 5. Results report (copy into the PR)

| Acceptance criterion | Evidence | Status |
|---|---|---|
| Member features implemented and integrated with the RTOS/config foundation | PRs in the inventory; `test_integration`; H1 fake build on a devkit | Synthetic: pass. Devkit: blocked |
| Synthetic tests exercise the real decoder, calibration/control, calculations, CSV | `test_integration` (10 tests), `docs/synthetic-run.csv`, mutation table | Pass |
| Hardware CAN/ECU and HX711 checks with traceable inputs | H2, H3 | Blocked (no board/ECU/load cell) |
| 13 approved signals present, nothing extra | `test_can`, CSV header (27 columns), H2 | Synthetic: pass. Hardware: blocked |
| Known-load calibration, tare, restart, geometry, shaft verified and documented | rows 5, 6, 9; H4, H5 | Synthetic: pass. Hardware: blocked; geometry open |
| CSV matches schema, real values, missing/stale shown, coherent calibration state | `check_csv.py` on the synthetic run; H7 capture | Synthetic: pass. Real capture: blocked |
| Failure matrix shows independent acquisition, bounded waiting/buffering, diagnostics | rows 1–9; H7 | Synthetic: pass. Hardware: blocked |
| Second member follows the operating guide | H8 | Blocked; reviewer ______ |

Defects found: ______ (link issues; a configuration defect goes to #31, a feature defect back to its ticket). Don't change an expected answer to make a step pass; record it as a defect instead.

## 6. Troubleshooting order

- **RPM wrong:** check the decode first (`# can` counters, [can-decoding.md](can-decoding.md)) before looking at the maths.
- **Force wrong:** raw counts → tare → scale, in that order (`loadcell_raw`, `cal status`).
- **Power wrong:** torque units, then whether the RPM is the load-cell shaft's.
- **Output freezes:** find which queue or hardware wait is stuck (`# output queue_free`, `# loadcell timeouts`, `# can` counters). Don't add delays.
- A quiet sensor is **not** a measured zero: blank means "no current value", `0` means "measured zero".
