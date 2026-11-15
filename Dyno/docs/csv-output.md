# CSV serial output (#38)

The dyno board prints one **CSV row** (comma-separated values) over USB serial every `CSV_ROW_INTERVAL_MS`. Each row is a **snapshot**: a copy of the latest load-cell sample, ECU values and torque/power result, taken together. The inputs were not necessarily measured at the same instant; their freshness is checked against `timestamp_ms`.

Lines that start with `# ` are **diagnostics** (warnings, status, errors), not data. CSV tools must skip them. They are always whole lines and never interrupt a row.

| Piece | Where | What it does |
| --- | --- | --- |
| `CsvSchema.h/.cpp` | `lib/DynoCore/src/` | **The one definition of the 27 columns.** Both the header and the row formatter walk this list, so their order cannot drift. |
| `CsvRow.h/.cpp` | `lib/DynoCore/src/` | `takeSnapshot()` (freshness, validity, torque/power) and `formatCsvRow()` (one row, bounded buffer). |
| `CsvPublisher.h/.cpp` | `lib/DynoCore/src/` | Header once, rows, `# ` lines, drop/overflow counters. Hardware-free, so it is tested on the host. |
| `CsvTask` | `src/Tasks/` | Every `CSV_ROW_INTERVAL_MS`: copies the mailboxes, builds the row, queues it. Never waits for inputs or for the serial port. |
| `OutputTask` | `src/Tasks/` | **The only task that writes to Serial.** Prints whole lines from the output queue, one at a time. |
| `tools/check_csv.py` | `tools/` | Checks a saved capture against this spec. |

## Row interval (TBD)

`CSV_ROW_INTERVAL_MS = 100` (10 rows/s) in `include/Defines/DynoConfig.h`. **This is a placeholder: the team still has to choose the rate.** Change that one number; `static_assert`s refuse values outside 20–1000 ms, values that are not faster than the input freshness limits, and rates the serial speed can't carry.

Rows come at a fixed rate (`vTaskDelayUntil`), even before CAN arrives or while the load cell or CAN is disconnected. Missing inputs give blank cells, not missing rows.

## Header

Printed **once**, at startup, before any row:

```csv
schema_version,timestamp_ms,can_received_mask,can_fresh_mask,rpm,lambda_avg,lambda_a,lambda_b,fuel_inj_pulse_width_ms,fuel_inj_duty_pct,knock_detected,rev_limit_rpm,lambda_target,battery_v,intake_air_temp_c,coolant_temp_c,gear,loadcell_raw,force_n,torque_nm,power_w,horsepower_hp,loadcell_valid,calibration_ready,calibration_version,tare_version,output_dropped_rows
```

**A late serial-monitor connection may miss the header** (and the first lines). To start a new bench capture, open the monitor first, then restart the board (reset button, or replug USB). `check_csv.py` reports data that arrives before a header.

## Column dictionary

General rules:
- Cells are numbers or empty. No text, so no quoting or escaping is needed. If text is ever added, quote cells containing commas, quotes or newlines and double embedded quotes (RFC 4180), and bump `schema_version`.
- A decimal point, no thousands separators, fixed precision per column (below). Lines end in CRLF (`\r\n`).
- **Empty = unavailable** (never received, stale, or not computable). A valid zero is printed as `0` (or `0.000`), never blank.
- A row can mix fresh and blank fields, for example load data present with RPM and power blank.

| # | Column | Unit | Format | Blank when |
| --- | --- | --- | --- | --- |
| 1 | `schema_version` | – | `1` | never |
| 2 | `timestamp_ms` | ms since boot | integer | never |
| 3 | `can_received_mask` | bit mask | decimal integer 0–127 | never |
| 4 | `can_fresh_mask` | bit mask | decimal integer 0–127 | never |
| 5 | `rpm` | rpm | integer | 0x520 not fresh |
| 6 | `lambda_avg` | λ (dimensionless) | 3 decimals | 0x520 not fresh |
| 7 | `lambda_a` | λ | 3 decimals | 0x521 not fresh |
| 8 | `lambda_b` | λ | 3 decimals | 0x521 not fresh |
| 9 | `fuel_inj_pulse_width_ms` | ms | 2 decimals | 0x522 not fresh |
| 10 | `fuel_inj_duty_pct` | % | 1 decimal | 0x522 not fresh |
| 11 | `knock_detected` | 0/1 | integer | 0x526 not fresh |
| 12 | `rev_limit_rpm` | rpm | integer | 0x526 not fresh |
| 13 | `lambda_target` | λ (signed) | 3 decimals | 0x527 not fresh |
| 14 | `battery_v` | V | 2 decimals | 0x530 not fresh |
| 15 | `intake_air_temp_c` | °C | 1 decimal | 0x530 not fresh |
| 16 | `coolant_temp_c` | °C | 1 decimal | 0x530 not fresh |
| 17 | `gear` | DBC `GearPosn` raw code | integer | 0x536 not fresh |
| 18 | `loadcell_raw` | signed HX711 ADC counts | integer | `loadcell_valid` = 0 |
| 19 | `force_n` | N | 3 decimals | no calibrated force (see below) |
| 20 | `torque_nm` | N·m | 3 decimals | force unusable, or geometry unconfirmed (#37) |
| 21 | `power_w` | W | 1 decimal | torque unusable, RPM missing/stale, sources too far apart in time, or same-shaft unconfirmed (#37) |
| 22 | `horsepower_hp` | mechanical hp (745.7 W) | 3 decimals | same as `power_w` |
| 23 | `loadcell_valid` | 0/1 | integer | never |
| 24 | `calibration_ready` | 0/1 | integer | never |
| 25 | `calibration_version` | – | integer | force unavailable |
| 26 | `tare_version` | – | integer | force unavailable |
| 27 | `output_dropped_rows` | rows | integer | never |

Details:
- **`timestamp_ms`** is the ESP32's `millis()`, an unsigned **32-bit** counter. It wraps back to 0 after 2³² ms ≈ **49.7 days** of uptime. Freshness maths uses unsigned subtraction, so it stays correct across the wrap; a capture spanning the wrap shows the timestamp jump down.
- **CAN masks:** bit 0 = `0x520`, 1 = `0x521`, 2 = `0x522`, 3 = `0x526`, 4 = `0x527`, 5 = `0x530`, 6 = `0x536`. *Received* = a valid frame has arrived since boot; *fresh* = the last one is younger than `CAN_FRESHNESS_MS` (2000 ms). All seven = 127. Fresh bits are always a subset of received bits. Example: `127,95` = everything received, but `0x530` (bit 5, value 32) is stale.
- **ECU values** (`rpm` … `gear`) are the 13 signals from #33, already scaled to engineering units with the DBC's signed/unsigned rules. The decimals are set by the DBC scale (0.001 → 3 decimals) and printed with integer maths, so nothing is lost to rounding. `gear` is the DBC's raw `GearPosn` value, not reinterpreted.
- **`loadcell_valid`** = 1 only for a fresh usable sample: status `Valid` and younger than `FORCE_FRESHNESS_MS` (500 ms).
- **Force, versions, `calibration_ready`** come from the calibrator in the load-cell task (#35, see [calibration.md](calibration.md)). Until the load cell has been calibrated once, `force_n`, `torque_nm`, `power_w`, `horsepower_hp` and both versions are blank, `calibration_ready` is 0, and a `# WARNING: load cell not calibrated` line says so. While a calibration or tare is collecting, `calibration_ready` is 0 and the force is blank too. The versions printed are the pair carried by the force measurement used for this row. Readiness, timestamps and versions are frozen in the snapshot before the row is queued, so a queued row can't pick up newer metadata.
- **Torque and power** come from #37's `computeTorquePower()`. Reasons for a blank value appear in the `# calc` status line.
- **`output_dropped_rows`**: rows discarded since boot because the output queue was full (see below). It never goes down until a restart.

## Example rows

[`csv-sample.csv`](csv-sample.csv) is a short example capture (CRLF line endings). A host test (`test_csv_sample`) rebuilds every row from matching inputs and checks the firmware formatter produces exactly these bytes, so the sample cannot go out of date.

```csv
1,0,0,0,,,,,,,,,,,,,,,,,,,0,0,,,0
1,1500,0,0,,,,,,,,,,,,,,120000,,,,,1,0,,,0
1,2500,127,127,6000,1.000,0.990,1.010,12.50,45.5,0,11000,0.950,13.80,25.3,90.5,3,120000,,,,,1,0,,,0
1,2600,127,127,6000,1.000,0.990,1.010,12.50,45.5,0,11000,0.950,13.80,25.3,90.5,3,120000,100.000,25.000,15708.0,21.065,1,1,2,3,0
1,12000,127,95,6000,1.000,0.990,1.010,12.50,45.5,0,11000,0.950,,,,3,120000,,,,,1,0,,,0
1,28000,127,127,6000,1.000,0.990,1.010,12.50,45.5,0,11000,0.950,13.80,25.3,90.5,3,,,,,,0,0,,,0
1,30000,127,127,6000,1.000,0.990,1.010,12.50,45.5,0,11000,0.950,13.80,25.3,90.5,3,120000,,,,,1,0,,,3
```

- `0` ms: startup, nothing received yet: every measurement is blank.
- `1500`: load cell running, ECU silent.
- `2500`: every ECU message fresh; force blank (no calibration).
- `2600`: **fixture only** (not a real capture; the arm isn't measured yet): a 100 N calibrated force with a 0.25 m arm gives 25 N·m, and at 6000 rpm 15708 W = 21.065 hp.
- `12000`: `0x530` stale: battery and both temperatures blank, everything else kept.
- `28000`: load-cell fault: `loadcell_valid=0`, raw blank.
- `30000`: 3 rows have been dropped so far.

## `# ` lines

| Line | When |
| --- | --- |
| `# WARNING: ...` | After the header, then every 10 s (`DIAGNOSTIC_REPEAT_INTERVAL_MS`), because USB serial often misses boot messages. Unconfirmed pins, geometry or same-shaft setup; load cell not calibrated. |
| `# FAKE INPUTS: ...` | Same timing, only in the `esp32-s3-fake` build. Normal builds never invent measurements. |
| `# loadcell raw=... status=... samples=...` | Every 5 s (`STATUS_LINE_INTERVAL_MS`). See [loadcell-acquisition.md](loadcell-acquisition.md). |
| `# can link=... hz=... unknown=...` | Every 5 s. See [can-decoding.md](can-decoding.md). |
| `# calc torque_nm=-(Reason) ...` | Every 5 s, from the same snapshot as the row just before it. See [torque-power.md](torque-power.md). |
| `# output rows_dropped=... diagnostics_dropped=... row_overflows=... queue_free=x/16` | Every 5 s: the health of the output path. |
| `# ERROR: CSV row too long, not printed (N so far)` | A row didn't fit its 512-byte buffer (shouldn't happen: a worst-case row is ~250 bytes). The row is skipped, never printed cut short. |
| `# ERROR: diagnostic line too long, not printed` | Same, for a `# ` line. |
| `# ERROR: <message>` (repeating) | Startup failed (out of memory). The board stops. |
| `# cal ...`, `# tare ...`, `# cmd: ...` | Replies to commands typed into the serial monitor, and the calibration found at boot (#35). See [calibration.md](calibration.md). |

Other code adds its own `# ` lines with `printDiagnostic()`, which any task can call. It never waits: if the queue is full, the line is dropped and counted in `diagnostics_dropped`.

## How output works (and what happens when the PC is slow)

```
LoadCellTask ─┐ mailboxes                         output queue (16 lines)
CanTask ──────┴──────────▶ CsvTask ── row / # line ──▶ [][][]...[] ──▶ OutputTask ──▶ USB serial
              (copied, no locks held)    (never waits)                   (only writer)
```

- **Bounded memory:** each line is a fixed 512-byte buffer and the queue holds `CSV_OUTPUT_QUEUE_LINES` (16) lines, so output never uses more memory, however long the PC stops reading.
- **When the queue is full** (PC not reading, or reading too slowly), the **new** row is thrown away and `output_dropped_rows` goes up by one. Load cell and CAN keep running, because nothing upstream ever waits for the serial port. Rows that were already queued are printed whole, so a capture can have a gap in `timestamp_ms` but never a broken row. Over the gap, `output_dropped_rows` rises by the number of missing rows.
- **Whole lines only:** `OutputTask` only gives the USB driver as many bytes as it has room for right now, and waits otherwise. (The ESP32-S3 USB driver's `write()` otherwise gives up after a timeout when the PC isn't reading and would send half a line.) The driver's transmit buffer is raised to 1024 bytes (`Serial.setTxBufferSize` in `main.cpp`).

## Serial settings

- The board talks over the ESP32-S3's **native USB** port (`ARDUINO_USB_MODE=1`, `Serial` = USB CDC). Over native USB the baud number isn't actually used, but the monitor still asks for one.
- **Baud: 115200**, set once in `platformio.ini` (`[dyno] serial_baud`). It feeds both `monitor_speed` and the firmware (`-DDYNO_SERIAL_BAUD`), so they can't disagree. If the board is ever read through a UART adapter instead, this number matters; a `static_assert` checks it can carry the row rate.
- 8 data bits, no parity, 1 stop bit (8N1), the PlatformIO default.

## Capturing and checking

A capture is just the serial text saved to a file. With PlatformIO:

```sh
pio device monitor -f log2file      # writes platformio-device-monitor-<date>.log in the current folder
# open the monitor first, THEN reset the board, so the header is captured
```

Any serial terminal that saves raw text works. *Not yet tried on hardware.* Then check it:

```sh
python3 tools/check_csv.py capture.log
```

It checks the header, 27 cells per row, numeric-or-empty cells, always-present columns, `schema_version`, masks (≤ 127, fresh ⊆ received), 0/1 flags, `loadcell_raw` blank when not valid, `output_dropped_rows` never decreasing, data before a header (late capture), and a cut-off last line. It skips `# ` lines; a second header (board restarted) starts the checks over. Exit code 0 = OK, 1 = problems found (each listed with its line number), 2 = usage error. Python 3.9+, standard library only. For spreadsheets or pandas, skip lines starting with `#` (`pd.read_csv(path, comment='#')`).

## Tests

| Command | What | Hardware |
| --- | --- | --- |
| `pio test -e native -f test_csv` | Header, every column, startup blanks, valid zeros, signed values, all 13 signals, each CAN message going stale on its own, load-cell loss, calibration/tare versions, missing RPM blanks only power (25 tests) | none |
| `pio test -e native -f test_csv_publisher` | Header once, full queue drops the new row and counts it, row/diagnostic overflow becomes an `# ERROR` line, never a cut row (9 tests) | none |
| `pio test -e native -f test_csv_pipeline` | Fake CAN + fake load cell → acquirers → publisher → small queue, with a fast, slow and jammed "PC": every missing row is counted, rows stay whole, acquisition keeps going (4 tests) | none |
| `pio test -e native -f test_csv_sample` | `docs/csv-sample.csv` matches the formatter byte for byte (2 tests) | none |
| `python3 tools/test_check_csv.py` | The capture checker (12 tests) | none |
| `pio run -e esp32-s3-fake` then upload | Whole firmware with fake inputs: CSV rows following the demo timeline | devkit |

## Hardware checklist (not yet done; physical end-to-end validation is #39)

Everything above was tested **on the host only**. Fill in on the bench:

1. **Header.** Open the monitor, reset the board. The first line is the header, then the `# WARNING` lines, then rows. Result: ______
2. **Rate.** `timestamp_ms` goes up by 100 per row (± a few ms); over 60 s, about 600 rows. Result: ______
3. **Clean capture.** Capture 2 minutes with `pio device monitor -f log2file`; `python3 tools/check_csv.py <file>` exits 0. Result: ______
4. **CAN off.** With the ECU off, rows keep coming with masks `0,0` and ECU cells blank. Result: ______
5. **Load cell unplugged.** Rows keep coming with `loadcell_valid=0`. Result: ______
6. **Slow/stalled PC.** Pause the terminal (or stop reading) for ~10 s, then resume. Expect: no broken rows, a gap in `timestamp_ms`, and `output_dropped_rows` up by about the number of missing rows; the `# loadcell` sample count kept rising. Result: ______
7. **Late connect.** Connect the monitor without resetting: the header is missing (expected); the first line may be partial. `check_csv.py` must flag it. Result: ______
8. **Unplug/replug USB** while running: expected: the board keeps running (unplugging native USB should not reset it) and rows resume. Result: ______
9. **Fake build.** `esp32-s3-fake`: `# FAKE INPUTS` appears, and the masks follow the demo timeline in [can-decoding.md](can-decoding.md). Result: ______

**If columns drift**, compare the header and the formatter (both come from `kCsvColumns`). **If rows stop during CAN silence**, look for a blocking wait in `CsvTask` (there shouldn't be one). **If values look wrong**, check the producer's validity and units before touching the formatting.
