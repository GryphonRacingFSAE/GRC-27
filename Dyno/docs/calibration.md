# Load-cell calibration and tare

How the dyno turns raw HX711 counts into newtons (issue #35), and the operator's tare button (#36): the serial commands, the stability rule, what is saved and how, and how to check it on hardware.

## What it does

The load cell gives a raw number (HX711 **counts**) that rises or falls with the force on it. To get newtons we need two numbers:

- **offset**: the counts with nothing on the arm (the zero)
- **scale**: how many counts one newton adds (`counts/N`)

You find them by reading the load cell twice: once **unloaded** and once with a **known reference load** hung on it. The dyno saves the result in the ESP32's flash, so it survives power-off. A **tare** re-measures only the zero, for example when the reading has drifted.

```
CommandTask ── LoadCellCommand ──► LoadCellTask ─────────────────► force mailbox ──► CsvTask (force_n, torque, power)
 (serial in,    (command queue)     LoadCellAcquirer → LoadCellCalibrator
 tare button)                                            │  ▲
                                          save / load    ▼  │
                                                     NVS (flash)
```

- **CommandTask** reads what you type in the serial monitor, one line at a time, and checks it is a valid command. It also polls the **tare button** (#36) every 20 ms; a press queues the same `tare` command. It never prints anything itself: its replies are `# cmd: ...` lines sent through `printDiagnostic()`, so OutputTask stays the only serial writer.
- **LoadCellTask** is still the only code that touches the HX711. It now also owns the **calibrator**. Every pass it hands the calibrator the latest sample, and the calibrator returns the force to publish. The calibrator never reads the HX711 itself and never waits.
- The published `ForceMeasurement` carries the **calibration and tare versions it was computed with**. A force and its versions travel together through the mailbox, so a row can never pair a force with the wrong versions.

Code: `lib/DynoCore/src/LoadCellCalibrator.*` (procedure), `StableWindow.*` (settling), `CalibrationMath.*` (formulas), `CalibrationRecordCodec.*` (stored format), `CommandParser.*` and `LineReader.*` (typed input), `ButtonDebouncer.*` and `TareButton.*` (the button). The ESP32-only parts are `lib/DynoHardware/src/PreferencesCalibrationStore.*` (flash), `lib/DynoHardware/src/GpioButton.*` (the button pin) and `src/Tasks/CommandTask.cpp` (serial input and button polling).

## Commands

Type these into the serial monitor (`pio device monitor`) and press Enter. Upper or lower case both work. Extra spaces and tabs are fine.

| Command | What it does |
|---|---|
| `cal unloaded` | Step 1: take the unloaded reading. Nothing may hang on the arm. |
| `cal loaded <value>N` | Step 2: take the loaded reading with a known **force** on the arm, e.g. `cal loaded 100N`. |
| `cal loaded <value>kg` | Same, with a known **mass**, e.g. `cal loaded 10.2kg`. Converted with standard gravity 9.80665 m/s² (10.2 kg = 100.028 N). |
| `cal cancel` | Abandon a calibration or tare in progress. The previous calibration stays. |
| `cal status` | Print the active calibration, its versions, and what the calibrator is doing. |
| `tare` | Re-measure the zero only. Nothing may hang on the arm. Only works once calibrated. |

The **unit is required**. `cal loaded 100` is refused, because guessing wrong between N and kg would silently scale every torque reading by 9.8. The value must be a plain positive decimal number: `100`, `100.5`, `.5` and `5.` are fine, but `-5`, `+5`, `1e3`, `inf` and `nan` are not. A line can be at most 64 characters.

A line the dyno doesn't understand gets a reply and is otherwise ignored:

```
# cmd: "cal loaded 100" not understood (MissingUnit); commands: tare, cal unloaded, cal loaded <value>N|kg, cal cancel, cal status
# cmd: line ignored (longer than 64 characters)
# cmd: line ignored (unexpected control or non-ASCII character; check the baud rate)
```

The reasons are `Empty`, `UnknownCommand`, `ExtraWords`, `MissingValue`, `BadNumber`, `MissingUnit`, `BadUnit` and `NotPositive`.

## Calibration procedure

1. Take everything off the arm and keep it still. Type `cal unloaded`.
   ```
   # cal: collecting the unloaded reading; keep the arm still with nothing hanging on it
   # cal: unloaded reading 100000.0 counts (spread 0, 10 samples); hang the reference load, then type "cal loaded <x>N" or "cal loaded <x>kg"
   ```
2. Hang the reference load **in the direction the engine pushes** (this sets the sign of force, see [torque-power.md](torque-power.md) "Sign convention"). Wait for it to stop swinging. Type `cal loaded 100N` (or the mass in kg).
   ```
   # cal: collecting the loaded reading (100.000 N); keep the load still
   # cal OK: offset=100000.0 counts scale=200.0000 counts/N reference=100.000 N calibration_version=1 tare_version=1 (saved)
   ```
3. Remove the load. `force_n` in the CSV should read about 0. Hang it again: it should read the reference force.

If step 1 was wrong (something was touching the arm), type `cal unloaded` again before `cal loaded`; it replaces the unloaded reading. `cal cancel` abandons the whole procedure.

**During the whole procedure** (from `cal unloaded` until `cal OK`, `FAILED` or `cancel`, including the wait between steps 1 and 2), the CSV shows `calibration_ready=0` and `force_n`, torque and power are **blank**. Nothing measured while the arm is being loaded by hand ever ends up in a run's data. The torque/power reason reads `ForceUnavailable`.

## The maths

```
scale (counts/N) = (loaded counts - unloaded counts) / reference force (N)
force (N)        = (raw counts - offset) / scale
```

Worked example (the one the tests use): unloaded 100000, loaded 120000 under 100 N gives a scale of **200 counts/N**. A later raw reading of 110000 is then (110000 − 100000) / 200 = **50 N**.

The scale may be **negative**, if the counts fall when the load is applied. That is fine: the force still comes out positive in the direction the reference load was hung. There is no separate sign-flip setting, so a load cell mounted backwards can't be hidden by a config value.

A calibration is **rejected** (`cal FAILED: calibration rejected: <reason>`) when:

| Reason | Meaning |
|---|---|
| `SeparationTooSmall` | Loaded and unloaded differ by less than `CAL_MIN_SEPARATION_COUNTS` (5000). The reference load barely changed the reading: too light, not actually hanging, or the bridge isn't connected. |
| `ReferenceForceInvalid`, `CountsInvalid`, `ScaleInvalid` | Can't happen with the parser's checks, but guarded anyway. |

## Stability rule

The calibrator doesn't take one sample: it waits for the reading to **settle**, then averages. Code: `StableWindow`. Settings: `DynoConfig.h`.

- It looks at the samples from the **last `CAL_WINDOW_MS` (1 s)**. Once collection has run for a whole window, the reading is **stable** when that window holds **at least `CAL_MIN_SAMPLES` (8)** samples and **max − min ≤ `CAL_MAX_SPREAD_COUNTS` (500)**. The result is their **mean**.
- The window **slides**: the wobble while you hang a weight just ages out, and it succeeds as soon as the last second is calm.
- It **fails at once** on a load-cell fault (`SensorFault`) or a reading with |raw| ≥ `LOADCELL_SATURATION_COUNTS` (8,388,000), which means maxed out (overloaded, or the bridge wires unplugged: `Saturated`).
- It **fails after `CAL_TIMEOUT_MS` (10 s)** if it never settled: `InsufficientSamples` (fewer than 8 samples in the last second: the HX711 is too slow or keeps dropping out) or `Unstable` (enough samples but always too much spread: the arm is still moving or the noise is bigger than the limit).

```
# cal FAILED: reading not stable: Unstable; previous calibration kept
# tare FAILED: reading not stable: Saturated; previous zero kept
```

At 10 Hz a stable reading takes just over 1 s; at 80 Hz the same. The window holds at most 128 samples (enough for 1 s at 80 Hz).

## Versions

Every force carries two counters, printed in the CSV as `calibration_version` and `tare_version`:

| Event | `calibration_version` | `tare_version` | Offset | Scale |
|---|---|---|---|---|
| Successful calibration | +1 | +1 | new | new |
| Successful tare | same | +1 | new | same |
| Any failure, cancel, refused command | same | same | same | same |

- Both start from 0 when nothing is stored, so the first calibration gives 1/1.
- They are 32-bit and wrap from 4,294,967,295 to 0.
- A calibration's versions are saved with it, so they carry on after a reboot.
- The new offset, scale and versions are swapped in **together**, between two samples, by the load-cell task. No force is ever computed with a new offset and an old scale.
- The versions are blank in the CSV whenever the force is unusable (no calibration, calibrating, fault, stale). See [torque-power.md](torque-power.md) "Calibration and tare changes".

## Tare

`tare` collects a stable unloaded reading with the same rule and makes it the new offset. The scale doesn't change. It needs a calibration first (`tare: refused, not calibrated`), and it can't overlap a calibration (`tare: busy with a calibration; type "cal cancel" first`).

```
# tare: collecting; keep the arm still with nothing hanging on it
# tare OK: offset=101000.0 counts calibration_version=1 tare_version=2 (not saved: tare again after a reboot)
```

**A tare is not saved** (team decision, 2026-11-09). After a reboot the offset goes back to the one measured during the last calibration. Tare again after every power-up. A consequence: `tare_version` also goes back to the saved value after a reboot, so the same `tare_version` number can appear in two boots with different zeros. Compare versions **within one boot** (one CSV header to the next).

## Tare button (#36)

A push button on the proto board does the same as typing `tare`. Press it once, firmly, with nothing hanging on the arm and the arm still. You see:

```
# button: tare pressed (presses=1 bounces=0)
# tare: collecting; keep the arm still with nothing hanging on it
# tare OK: offset=101000.0 counts calibration_version=1 tare_version=2 (not saved: tare again after a reboot)
```

The button only **sends the request**. The tare itself, and every refusal, is exactly the typed command's: `tare: refused, not calibrated` before a calibration, `tare: busy ...` during a calibration or while a tare is already collecting (an impatient second press), and `tare FAILED ...` if the reading won't settle. A press never changes anything on its own.

How a press is recognised:

- **Debounce.** A button's contacts flicker for a few ms when pressed or released ("bounce"). CommandTask reads the pin every 20 ms (`COMMAND_POLL_INTERVAL_MS`); a press counts once the pin has read "pressed" steadily for `TARE_DEBOUNCE_MS` (50 ms), timed from the last flicker. So a press is accepted 60–80 ms after the finger goes down.
- **One tare per press.** Holding the button gives one tare, not one every 50 ms. Releasing does nothing. Press again for another tare.
- **Held at power-up = ignored.** If the button reads pressed at the first poll (held during boot, stuck, or wired the other way round from `TARE_BUTTON_ACTIVE_LOW`), it does nothing until it has been released and pressed again. A stuck button can't keep taring.
- **Counters.** `presses` is the number of accepted presses since boot. `bounces` counts level changes that were undone before lasting 50 ms (contact chatter or electrical noise). A few per press is normal; if it climbs with nobody touching the button, the wiring is picking up noise.
- **Queue full.** If the command queue is full at that moment (only if several commands were typed at once), you get `# button: tare dropped (too many commands at once); press again`. The press is not saved for later, so nothing tares behind your back.

Wiring (`include/Defines/pindefs.h`, both **TBD** until the proto board exists, see [PLACEHOLDERS.md](../PLACEHOLDERS.md)):

| Setting | Now | Meaning |
|---|---|---|
| `PIN_TARE_BUTTON` | `0` | GPIO0, the devkit's **BOOT** button, so the feature works on a bare devkit. Change it to the proto board's button pin. |
| `TARE_BUTTON_ACTIVE_LOW` | `true` | Button between the pin and GND; the ESP32's internal pull-up holds the pin HIGH when released. `false` = button to 3.3 V, internal pull-down. |
| `TARE_DEBOUNCE_MS` (`DynoConfig.h`) | `50` | How long a press must last. The build refuses less than two polls (40 ms) or more than 150 ms. |

> **GPIO0 warning.** GPIO0 is a *strapping pin*: if it is held LOW while the ESP32 powers up or resets, the chip starts in download (flashing) mode and the dyno firmware **does not run** (the monitor shows `waiting for download`). Don't hold the tare button during power-up while it is on GPIO0. When choosing the proto board's pin, prefer an ordinary GPIO (not 0, 3, 45 or 46, which are strapping pins; not 19/20, the USB serial; not 26–37, flash/PSRAM). `test_config` rejects the pins that can't work at all.

## What is saved, and boot

The calibration is kept in the ESP32's **NVS** ("non-volatile storage": a small key-value store in a flash partition, which survives power-off and re-flashing the firmware). Namespace `dyno` (the fake-inputs build uses `dyno-fake`, so trying commands on a devkit never touches a real calibration), key `cal`, one 76-byte record:

| Bytes | Field | Type |
|---|---|---|
| 0–3 | magic `DCAL` | text |
| 4–5 | schema (1) | u16 |
| 6–7 | length (76) | u16 |
| 8 | offset (counts) | f64 |
| 16 | scale (counts/N) | f64 |
| 24 | reference force (N) | f64 |
| 32 | reference mass (kg), NaN if given in N | f64 |
| 40 | gravity used (m/s²), NaN if given in N | f64 |
| 48 | unloaded reading (counts) | f64 |
| 56 | loaded reading (counts) | f64 |
| 64 | `calibration_version` | u32 |
| 68 | `tare_version` | u32 |
| 72 | CRC-32 of bytes 0–71 | u32 |

All little-endian. The CRC is a checksum: if any byte is damaged, it no longer matches.

**At boot** the record is read and checked. The calibrator says what it found:

```
# cal: loaded the saved calibration: offset=100000.0 counts scale=200.0000 counts/N calibration_version=1 tare_version=1
# WARNING: no calibration stored (Missing): force, torque and power are blank until calibrated
```

| Found | Meaning | Result |
|---|---|---|
| `Ok` | A good record | Used |
| `Missing` | Nothing saved yet (new board, or NVS erased) | No force |
| `Corrupt` | Wrong magic, wrong length, CRC mismatch, or numbers that make no sense (zero/NaN scale) | No force |
| `UnsupportedSchema` | Saved by a different record version of the firmware | No force: calibrate again |

A missing or damaged record **never** gives a 0 N force: force, torque and power stay blank, and `# WARNING: load cell not calibrated` repeats every 10 s until you calibrate. The boot line often comes before the PC connects; type `cal status` to see it again.

**Saving:** a calibration is only accepted once it is written **and read back byte for byte**. If that fails, it is rejected and the previous record is written back, so what is stored always matches what is in use:

```
# cal FAILED: could not save: storage write failed; previous calibration kept
```

The flash write happens inside the load-cell task and can take some milliseconds; the task skips a few HX711 polls then. Force is blank at that moment anyway (the calibration isn't finished).

## Settings and tuning

All in `include/Defines/DynoConfig.h`. **All are TBD**: first guesses, made without the real load cell. Listed in [PLACEHOLDERS.md](../PLACEHOLDERS.md).

| Setting | Value | Retune when |
|---|---|---|
| `CAL_WINDOW_MS` | 1000 ms | Longer = steadier mean, slower. Must hold `CAL_MIN_SAMPLES` at 10 Hz and fit 128 samples at 80 Hz (checked at build time). |
| `CAL_MIN_SAMPLES` | 8 | Raise at 80 Hz (e.g. 40) to average more. |
| `CAL_MAX_SPREAD_COUNTS` | 500 | Set from the measured unloaded noise (checklist step 3): about 3× the max − min you see with the arm still. Too low: calibration always `Unstable`. Too high: accepts a swinging weight. |
| `CAL_TIMEOUT_MS` | 10000 ms | If hanging the weight and letting it settle takes longer. |
| `CAL_MIN_SEPARATION_COUNTS` | 5000 | Set to well under (reference load × measured counts/N). E.g. 200 counts/N and a 100 N reference give 20000 counts, so 5000 is fine. |
| `LOADCELL_SATURATION_COUNTS` | 8,388,000 | Normally leave it. Near the HX711's ±8,388,607 limit. |

If you change these, update the fixture in `lib/DynoCore/src/CalibrationExamples.h` too; `test_config` fails until they match.

Command input (not tuning): `COMMAND_POLL_INTERVAL_MS` (20 ms), `COMMAND_MAX_BYTES_PER_POLL` (64), `COMMAND_QUEUE_LENGTH` (4). If commands are pasted faster than the load-cell task takes them: `# cmd: "..." dropped (too many commands at once); type it again`.

## Fake inputs (bare devkit)

In the `esp32-s3-fake` build the load cell is a ramp (1000 counts/s), so it never settles: every `cal unloaded` and `tare` ends with `FAILED: reading not stable: Unstable` after 10 s. That still checks the command path, the replies and `cal status` without an HX711. The fake build saves to its own NVS namespace (`dyno-fake`).

## Running the tests (no hardware)

```
pio test -e native -f test_calibration            # formulas, worked example, sign, kg (14 tests)
pio test -e native -f test_stable_window          # the stability rule (17 tests)
pio test -e native -f test_command                # line reader + command parser (9 tests)
pio test -e native -f test_calibrator             # the procedure with a fake store (30 tests)
pio test -e native -f test_calibration_pipeline   # fake load cell → calibrator → CSV rows, typed vs button (2 tests)
pio test -e native -f test_button                 # button debouncing with a fake clock (11 tests)
pio test -e native -f test_tare_button            # fake button → queue → real calibrator (11 tests)
```

`test_calibrator` uses `FakeCalibrationStore`, which can fail writes or garble the next one. It covers: boot with a missing, damaged (bad magic, truncated, CRC mismatch, NaN/zero scale) or other-schema record; the worked example; `calibration_ready=0` for the whole procedure; reboot keeps the calibration; recalibration; a write failure and a garbled write (old record restored); separation too small; unstable timeout; saturation; sensor fault; commands out of order; cancel; tare (refused uncalibrated, busy, offset only, not saved); `cal status`; the record layout byte by byte; the CRC check value; kg; counter wrap; and that every force matches its version pair.

`test_calibration_pipeline` runs a scripted 10 Hz rig through `cal unloaded`, `cal loaded 100N` and `tare`, and checks every CSV row: blank while calibrating, 100 N then 50 N under versions 1/1, blank during the tare, 50 N under 1/2 afterwards. It then runs the same session with the tare done by the **button**, and checks every CSV row is byte-for-byte the same as with the typed `tare`.

`test_button` scripts the pin level at each poll and works out every expected press time by hand: a clean press accepted at exactly 50 ms, holding doesn't repeat, two presses, contact chatter (timed from the last flicker), held at boot, a short release, a one-poll glitch, the `millis()` wrap after 49.7 days, a late poll, and both wirings. `test_tare_button` runs a fake button, a 4-slot queue and the real calibrator, polled and stepped like the two tasks: a press tares (and moves the zero), refused before calibration, busy during a calibration and during a tare, held 5 s = one tare, bounce counts in the line, held from boot never tares, active-high wiring, and a dropped press never tares later. `test_config` checks the button pin is a usable S3 input, the debounce spans two polls, and the real settings tap-to-tare.

**Not tested on the host** (no ESP32 there), so they're in the checklist below: the flash store (`PreferencesCalibrationStore`), the serial reading in `CommandTask`, and the button pin (`GpioButton`: the pin number, its pull resistor, real bounce). `test_device_tare_button` checks the button pin on a devkit (below).

## Hardware checklist (#35)

Needs the load cell mounted and wired (do the [loadcell-acquisition.md](loadcell-acquisition.md) checklist first) and a known reference mass. Write the results in the PR or the plan.

1. **Fresh board.** Flash with `pio run -t upload && pio device monitor`. Type `cal status`. Expect `not calibrated (Missing)` (or the saved one, if this board was calibrated before). The `# WARNING: load cell not calibrated` line repeats every 10 s.
2. **Typing.** Type `hello`, `cal loaded 100`, and a line of 70 characters. Each gets a `# cmd:` reply, and CSV rows keep coming. Try both line endings your monitor offers (LF and CRLF): one reply per line.
3. **Unloaded noise.** With nothing on the arm, watch `loadcell_raw` for 10 s. Note the max − min: ______ counts. `CAL_MAX_SPREAD_COUNTS` should be about 3× this (see "Settings and tuning").
4. **Calibrate.** Follow "Calibration procedure" with the reference mass, in the engine's push direction. Note: unloaded ______ counts, loaded ______ counts, scale ______ counts/N, versions ______. During the procedure every row has `calibration_ready=0` and a blank `force_n`.
5. **Check against known loads.** Fill in the table below. The sign should be positive in the push direction. **The accepted error is TBD** (a team decision, in [PLACEHOLDERS.md](../PLACEHOLDERS.md)); record the errors so it can be set.
6. **Reboot.** Press reset (or unplug USB). `cal status` shows the same offset, scale and versions as step 4. Hang the reference again: same force as before.
7. **Tare.** Hang a small mass (e.g. 1 kg) on the arm and leave it there; `force_n` reads about its weight. Type `tare`. Expect `tare OK` with `tare_version` +1 and the same `calibration_version`, and `force_n` now reads about 0 with the small mass on. Hang the reference too: `force_n` reads the reference force alone (the scale didn't change). Remove everything and reboot: the offset is back to step 4's, since a tare is not saved, so the small mass reads its weight again.
8. **Failures.** (a) Type `cal unloaded` and keep pushing the arm by hand: `FAILED ... Unstable` after 10 s, and `cal status` shows the old calibration unchanged. (b) Type `cal unloaded`, then `cal loaded 100N` with **no** load hung: `FAILED ... SeparationTooSmall`. (c) Unplug the HX711 during `cal unloaded`: `FAILED ... SensorFault`. (d) Unplug only the load-cell bridge wires during `tare`: `FAILED ... Saturated` (or `Unstable`; note which: ______).
9. **Re-flash keeps it.** Flash the firmware again (`pio run -t upload`, not an erase). `cal status` still shows the calibration. *(A full flash erase, `pio run -t erase`, deletes it: expected.)*
10. **Wrong unit check.** Note what `force_n` reads with the reference mass when calibrated in kg vs N for the same mass (`cal loaded 10.2kg` vs `cal loaded 100.03N`): the scales should match within 0.1%.

### Expected vs measured

| Load (kg) | Expected force (N) = kg × 9.80665 | Measured `force_n` | Error (%) |
|---|---|---|---|
| 0 (unloaded) | 0 | ______ | (N): ______ |
| ______ | ______ | ______ | ______ |
| ______ | ______ | ______ | ______ |
| ______ (reference) | ______ | ______ | ______ |
| ______ (≈ 1.5 × reference) | ______ | ______ | ______ |
| reference, opposite direction | −______ | ______ | ______ |

A straight line through all rows (equal error %) means the load cell is linear; a growing error at heavier loads means it isn't, or something is touching the arm.

Use at least one load that is **not** the reference (the issue asks for independent loads: the reference point always reads right, by construction).

### Repeatability and loading/unloading

11. **Repeatability.** Hang and remove the reference load 5 times. Note `force_n` each time it's on, and each time it's off.
12. **Loading vs unloading.** Add the masses one at a time going up, then take them off one at a time going down. Note `force_n` at each step in both directions. If the readings going down are consistently higher or lower than going up at the same load, the mounting is sticking or rubbing (hysteresis).

| Trial | Loaded `force_n` | Unloaded `force_n` |
|---|---|---|
| 1 | ______ | ______ |
| 2 | ______ | ______ |
| 3 | ______ | ______ |
| 4 | ______ | ______ |
| 5 | ______ | ______ |
| Spread (max − min) | ______ | ______ |

| Load (kg) | `force_n` going up | `force_n` going down | Difference |
|---|---|---|---|
| ______ | ______ | ______ | ______ |
| ______ | ______ | ______ | ______ |
| ______ | ______ | ______ | ______ |

Also write down, with the results: the date, who did it, which reference mass was used (and its certified or weighed value), the direction it was hung, and the `cal OK` line. The stored record keeps the numbers (reference, readings, versions), not free-text notes, so these notes live here and in the PR.

### Tare button (#36)

13. **Tare button (#36).** First on a bare devkit, with the button still on GPIO0 (BOOT): run `pio test -e esp32-s3-devboard -f test_device_tare_button` and follow its prompts (press once, then once more). It checks the idle pin reads released, one press = one tare, and prints the real bounce: level changes ______ over ______ ms. Then, with the firmware and a calibration (step 4):
    - Press the button once with a small mass hung: expect `# button: tare pressed (presses=1 bounces=…)` then `tare OK`, same result as step 7. `bounces` after 10 presses: ______.
    - Hold it for 5 s: one tare only.
    - Press it during `cal unloaded`: `tare: busy`, and the calibration carries on.
    - Leave it alone for a minute with the engine/fan running nearby: no `# button:` lines, `bounces` doesn't climb.
    - If the real bounce lasts longer than about 30 ms, raise `TARE_DEBOUNCE_MS` (and rework the test fixture, `test_config` will say).
    - On the proto board: change `PIN_TARE_BUTTON`/`TARE_BUTTON_ACTIVE_LOW` to the real wiring, rerun `test_device_tare_button`, then repeat the presses above.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Every force is about 9.8× too big or too small | The reference was given in the wrong unit (`100kg` instead of `100N`, or a mass typed as `N`). Recalibrate with the right unit. |
| Force is negative in the engine's push direction | Calibrated with the load in the opposite direction. Recalibrate, hanging the load in the push direction. Don't flip it anywhere else. |
| `cal FAILED: reading not stable: Unstable` every time | The arm is still moving, or the noise is bigger than `CAL_MAX_SPREAD_COUNTS`. Measure the noise (checklist step 3) and retune. |
| `... InsufficientSamples` | The HX711 is slower than expected or keeps timing out. Check the `# loadcell` line's `samples` and `timeouts`. |
| `... Saturated` | The reading is at the HX711's limit: overloaded, or the bridge wires (E+/E−/A+/A−) are loose. |
| `cal FAILED: calibration rejected: SeparationTooSmall` | The load didn't change the reading enough: not actually hanging, too light, or a loose bridge wire. |
| `cal FAILED: could not save` | NVS write failed. Retry once; if it keeps failing, the NVS partition may be full or damaged (`pio run -t erase` clears it, and also the calibration). |
| Boot says `Corrupt` | The stored record is damaged. Recalibrate. |
| Pressing the tare button prints nothing | Wrong `PIN_TARE_BUTTON`, or the button isn't connected. Run `test_device_tare_button`. |
| The tare button tares on **release** instead of press | `TARE_BUTTON_ACTIVE_LOW` is backwards for the wiring. |
| `# button: tare pressed` lines with nobody touching it | Noise on the button wire (long wire, no pull resistor strong enough). Check `bounces`; shorten/shield the wire or add an external 10 kΩ pull resistor. |
| Board doesn't start after power-up, monitor says `waiting for download` | The button on GPIO0 was held during power-up. Release and reset. |
| Boot says `UnsupportedSchema` | The firmware's record format changed. Recalibrate. |
| Typed commands get no reply | The monitor isn't sending line endings (turn on LF or CRLF), or the commands are going to another port. |
| `# cmd: line ignored (unexpected control ...)` | Wrong baud rate on a UART adapter, or line noise. Over native USB, check the monitor isn't sending special keys. |
