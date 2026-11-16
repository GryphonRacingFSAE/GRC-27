# Operating the dyno

How to flash the Dyno board, connect it, read what it prints, calibrate the load cell, zero it, and get going again when an input drops out. This is the guide for whoever is running the dyno; how each part works inside is in the other files in `docs/`.

> **Status (2026-11-15):** written and checked against the synthetic (host) tests only. No step here has been tried on the real board yet. Pins and the arm length are still placeholders ([PLACEHOLDERS.md](../PLACEHOLDERS.md)), so on the real board torque and power stay blank until the mechanical team confirms the geometry. A second member still has to follow this guide on hardware ([bench-test.md](bench-test.md) step H8).

## 1. What you need

- A computer with [PlatformIO](https://platformio.org/) (the VS Code extension or `pio` on the command line) and this repo.
- A USB cable to the board's USB port.
- **Hardware mode:** the Dyno board with the HX711 and load cell wired, the ECU CAN connected, and a known reference mass for calibration.
- **Synthetic mode:** any ESP32-S3 devkit. Nothing else.

All commands below are run from `GRC-27/Dyno/` (where `platformio.ini` is).

## 2. Synthetic mode vs hardware mode

| | Hardware mode | Synthetic (fake inputs) mode |
|---|---|---|
| Build | `pio run -t upload` | `pio run -e esp32-s3-fake -t upload` |
| Reads | the real HX711 and ECU CAN bus | a scripted load cell (a ramp that "unplugs" at 27 s of every 30 s) and a scripted ECU (a 25 s cycle that goes silent on purpose) |
| You'll see | `# WARNING` lines until pins/geometry are confirmed | `# FAKE INPUTS: ...` after the header and every 10 s |
| Use it for | real runs | trying the monitor, the commands and the capture on a bare devkit; training |

**Synthetic data is never a measurement.** Anything captured in synthetic mode (or from the host tests, like [synthetic-run.csv](synthetic-run.csv)) must be labelled synthetic wherever it's shared. If you see `# FAKE INPUTS`, you are in synthetic mode.

There's also a third, no-board-at-all option: `pio test -e native` runs everything on your computer (see [bench-test.md](bench-test.md)).

## 3. Build and flash

```sh
pio run -t upload && pio device monitor                    # hardware mode
pio run -e esp32-s3-fake -t upload && pio device monitor   # synthetic mode
```

If the upload can't find the board, hold the board's BOOT button, tap RESET, let go of BOOT, and upload again. Re-flashing keeps the saved calibration. `pio run -t erase` wipes it (you'll have to calibrate again).

## 4. Connect the setup (hardware mode)

Power everything **off** while connecting.

1. Load cell → HX711 (E+, E−, A+, A−), HX711 → board (VCC, GND, DOUT, SCK). Pin numbers: `include/Defines/pindefs.h`.
2. ECU CAN H/L → the board's CAN connector. The bus needs a 120 Ω terminator at each end (about 60 Ω measured across CANH–CANL with everything off).
3. USB to the computer. The ECU can be on or off; the dyno copes with either.

## 5. Open the monitor

`pio device monitor` opens it at the right speed (115200 baud). If you use another terminal: 115200, and turn on sending a line ending (LF or CRLF) so typed commands work.

The USB connection often misses the very first lines after a reset. To see the start, open the monitor first, then press RESET on the board.

### What startup looks like

```
# WARNING: no calibration stored (Missing): force, torque and power are blank until calibrated
schema_version,timestamp_ms,can_received_mask,can_fresh_mask,rpm,...,output_dropped_rows
# WARNING: pin assignments are unconfirmed placeholders (see include/Defines/pindefs.h)
# WARNING: dyno geometry unconfirmed (DynoConfig.h): torque and power are disabled
1,0,0,0,,,,,,,,,,,,,,,,,,,0,0,,,0
1,100,...
```

- Lines starting with `# ` are messages for you. CSV tools skip them.
- The line starting `schema_version,` is the **header**, printed once per boot, before any data row. A few `# ` lines may come just before or after it.
- Every other line is a **data row**, 10 per second.
- On a board that has been calibrated, the first line says `# cal: loaded the saved calibration: ... calibration_version=N ...` instead of the "no calibration" warning.
- The `# WARNING` lines repeat every 10 s until fixed, so you'll see them even if you connect late.
- Every 5 s there are status lines: `# loadcell ...`, `# can ...`, `# calc ...`, `# output ...`.

## 6. Is it waiting, working, or faulted?

A **blank cell** means "no current value". It never means 0. A `0` means a real measured zero.

| What you see | Means | Do |
|---|---|---|
| `can_received_mask=0`, all ECU cells blank | No ECU message has arrived since boot (ECU off, or not connected) | Turn on / connect the ECU. See [can-decoding.md](can-decoding.md) checklist step 4 if it stays 0. |
| `can_fresh_mask=127` | All 7 ECU messages arriving | Working |
| `can_fresh_mask` between 1 and 126 | Some messages are late or missing. Each bit is one message (1=0x520 RPM + lambda avg, 2=0x521 lambda A/B, 4=0x522 fuel pulse width + duty, 8=0x526 knock + rev limit, 16=0x527 lambda target, 32=0x530 battery/IAT/coolant, 64=0x536 gear); only those cells are blank | Check the ECU's CAN output settings for the missing ones |
| `can_fresh_mask=0` but `can_received_mask=127` | ECU was there, then went quiet for over 2 s (unplugged, ECU off, bus fault) | Check the CAN wiring. It comes back by itself. |
| `loadcell_valid=1`, `loadcell_raw` changes a little | Load cell working | — |
| `loadcell_valid=0`, `loadcell_raw` blank, `# loadcell ... status=Fault` | HX711 not answering (unplugged, wiring) | Check the HX711 wiring. It retries every second and recovers by itself. |
| `calibration_ready=0`, `force_n` blank | Not calibrated, or a calibration/tare is in progress | Calibrate (section 7), or wait for the `cal OK` / `tare OK` |
| `force_n` filled, `torque_nm`/`power_w` blank | Geometry not confirmed (see the `# WARNING`), or the last `# calc` line gives a reason | Expected until the mechanical setup is confirmed |
| `power_w` blank but `torque_nm` and `rpm` filled | RPM and force more than 250 ms apart (one of them is late), or the shaft isn't confirmed | Usually CAN just dropped out. See the `# calc` reason. |
| `output_dropped_rows` going up | The computer isn't reading fast enough; rows were thrown away (and counted) so the measurements keep going | Close other programs using the port; don't pause the monitor during a run |
| Nothing at all | Wrong port or baud, or the board isn't running | Check the port, 115200 baud, press RESET |
| `waiting for download` | The BOOT button (GPIO0, also the bench tare button) was held during power-up | Let go and press RESET |

## 7. Calibrate with a known load

Do this once on a new board, and again if the load cell is moved or remounted. It's saved and survives power-off and re-flashing.

1. Take everything off the arm. Keep it still. Type:
   ```
   cal unloaded
   ```
   Wait about a second for `# cal: unloaded reading ... counts ...; hang the reference load ...`.
2. Hang the reference mass **in the direction the engine pushes** (this makes engine torque positive). Wait for it to stop swinging. Type the mass **with its unit**:
   ```
   cal loaded 10.2kg
   ```
   (or the force: `cal loaded 100N`). Without a unit it's refused, on purpose.
3. Expect `# cal OK: offset=... scale=... counts/N ... calibration_version=N ... (saved)`.
4. Take the mass off: `force_n` ≈ 0. Put it back: `force_n` ≈ the reference force (kg × 9.80665).

While calibrating, `force_n`, torque and power are blank and `calibration_ready=0`, so nothing measured by hand ends up in the data. `cal cancel` stops it and keeps the old calibration. `cal status` shows what is saved now.

If it says `FAILED`, the old calibration is kept. The common reasons:

| Message ends with | Cause | Fix |
|---|---|---|
| `Unstable` | The arm moved, or the reading is noisier than expected | Keep it still and try again |
| `SeparationTooSmall` | The mass didn't change the reading enough (not hanging, too light, loose wire) | Check the mass is hanging and the bridge wires |
| `SensorFault` / `InsufficientSamples` | The HX711 dropped out during the reading | Fix the wiring, try again |
| `Saturated` | The reading is at the amplifier's limit | Overloaded, or loose bridge wires |
| `could not save` | Flash write failed | Retry; see [calibration.md](calibration.md) troubleshooting |

More detail: [calibration.md](calibration.md).

## 8. Zero it (tare) before each run

A tare re-measures the zero only; the scale from the calibration stays. Do it **after every power-up**, with **nothing hanging on the arm** and the engine stopped:

- press the tare button once, or
- type `tare`.

Expect `# tare OK: offset=... tare_version=N (not saved: tare again after a reboot)` and `force_n` ≈ 0. The `tare_version` column goes up by 1, so every row says which zero it was measured against.

- `tare: refused, not calibrated`: calibrate first (section 7).
- `tare: busy`: a calibration or another tare is already running; wait for it, or `cal cancel`.
- `tare FAILED: reading not stable`: something moved. The old zero is kept. Try again.
- Holding the button, or pressing it again quickly, gives one tare, not several.

**A tare is not saved.** After a reboot the zero goes back to the one from the calibration (and `tare_version` goes back to the saved one), so tare again.

## 9. Read the CSV

One row every 100 ms. The columns, in order, with units:

| Column | Unit / meaning |
|---|---|
| `schema_version` | Always `1` for this layout |
| `timestamp_ms` | ms since the board started (resets to 0 on reboot) |
| `can_received_mask` | Which of the 7 ECU messages have arrived since boot (bits as in section 6) |
| `can_fresh_mask` | Which arrived in the last 2 s. Only those messages' cells are filled. |
| `rpm` | engine rpm (ECU) |
| `lambda_avg`, `lambda_a`, `lambda_b`, `lambda_target` | λ (1.000 = stoichiometric) |
| `fuel_inj_pulse_width_ms` | ms |
| `fuel_inj_duty_pct` | % |
| `knock_detected` | 0/1 |
| `rev_limit_rpm` | rpm |
| `battery_v` | V |
| `intake_air_temp_c`, `coolant_temp_c` | °C |
| `gear` | gear the ECU reports (just a number from the ECU, not used in the maths) |
| `loadcell_raw` | raw HX711 counts (no unit). Blank if the load cell isn't current. |
| `force_n` | N, after calibration and tare. Positive = engine push direction. |
| `torque_nm` | N·m = force × arm |
| `power_w` | W = torque × rpm × 2π / 60 |
| `horsepower_hp` | mechanical hp = W / 745.699872 |
| `loadcell_valid` | 1 = a load-cell reading less than 0.5 s old |
| `calibration_ready` | 1 = calibrated and not in the middle of a calibration/tare |
| `calibration_version`, `tare_version` | Which calibration and zero this row's force used |
| `output_dropped_rows` | Total rows thrown away since boot because the computer was too slow |

Negative values are real (load the other way) and keep their sign. Details and examples: [csv-output.md](csv-output.md).

### Save a capture

```sh
pio device monitor -f log2file
```

Press RESET once it's open so the file starts with the header. Stop with Ctrl+C. Then check it:

```sh
python3 tools/check_csv.py <the .log file>
```

`OK` means the file matches the layout. It tells you if the header is missing (you connected late), a line got cut off, or the board restarted part way (a second header, which is fine). Open the file in a spreadsheet; the `# ` lines can be filtered out by their first character.

## 10. When an input drops out

Nothing has to be restarted. Each input recovers by itself and the rows never stop.

| Problem | What happens | To recover |
|---|---|---|
| ECU / CAN unplugged or off | Within 2 s the ECU cells go blank and `can_fresh_mask` drops. Power goes blank within 0.25 s. Force and the load cell carry on. | Reconnect: values return within about a second |
| HX711 unplugged | Within about 0.5 s: `loadcell_valid=0`, force/torque/power blank. RPM and the other ECU values carry on. | Reconnect: it's back within about 2.5 s with the same calibration. Re-tare if the arm was disturbed. |
| Only the load-cell bridge wires loose | The HX711 still "works" but `loadcell_raw` jumps to junk or a huge value | Fix the wires; recalibrate if the reading doesn't return to normal |
| Monitor paused / computer slow | Rows are dropped (and counted in `output_dropped_rows`); measuring carries on. A gap in `timestamp_ms` shows where. | Resume; nothing else to do |
| Board reset / power cut | Header again, calibration reloaded, tare lost | Tare again (section 8) |
| Calibration looks wrong after moving the load cell | — | Calibrate again (section 7) |

If something else is wrong, in this order: RPM wrong → check the CAN decode first ([can-decoding.md](can-decoding.md)); force wrong → raw counts, then tare, then calibration; power wrong → torque, then whether the ECU RPM is the load-cell shaft's ([torque-power.md](torque-power.md)); rows stop → see which `# ` status line stopped changing ([csv-output.md](csv-output.md)).

## 11. Serial commands

| Type | Does |
|---|---|
| `cal unloaded` | Calibration step 1 (nothing hung) |
| `cal loaded <x>N` / `cal loaded <x>kg` | Calibration step 2 with the known load |
| `cal cancel` | Stop a calibration or tare; old one kept |
| `cal status` | Show the saved calibration and what it's doing |
| `tare` | Zero (same as the button) |

Anything else gets a `# cmd: ... not understood` reply listing these.
