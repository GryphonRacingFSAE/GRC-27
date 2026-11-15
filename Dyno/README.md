# Gryphon Racing — Dyno

The Dyno board supports Gryphon Racing's dynamometer testing setup. It reads the load cell (HX711) and the MaxxECU's CAN data, computes torque and power, and prints everything as CSV over USB serial (10 rows/s for now) for capture during engine testing.

## Building and testing

Run these from this folder (`GRC-27/Dyno/`, where `platformio.ini` is):

| Command | What it does | Hardware needed |
|---|---|---|
| `pio test -e native` | Runs the host tests on your computer (Unity, with AddressSanitizer/UBSan) | None |
| `pio run` | Builds the ESP32-S3 firmware | None |
| `pio run -t upload && pio device monitor` | Flashes the board and opens the serial monitor (CSV output, see [csv-output.md](docs/csv-output.md)) | Dyno board |
| `pio run -e esp32-s3-fake -t upload && pio device monitor` | Same, but with a simulated load cell and ECU (`DYNO_FAKE_INPUTS=1`) | Any ESP32-S3 devkit |
| `python3 tools/check_csv.py capture.csv` | Checks a saved serial capture against the CSV schema (column count and names, masks, flags) | None |
| `python3 tools/test_check_csv.py` | Tests for that checker | None |
| `pio test -e esp32-s3-devboard -f test_device_can_loopback` | On-device CAN self-test of the real driver (add `--without-uploading --without-testing` to only build it) | ESP32-S3 devkit + 1 jumper wire |

Layout:
- `lib/DynoCore/`: hardware-free logic (load-cell acquisition, ECU CAN decoding and reception, torque/power calculation, CSV schema/formatting/publishing, fakes). Builds for both the ESP32 and the host tests.
- `lib/DynoHardware/`: ESP32-only adapters (HX711, TWAI CAN) behind DynoCore's interfaces.
- `src/`: ESP32-only code (FreeRTOS tasks: load cell, CAN, CSV aggregation, serial output).
- `include/Defines/pindefs.h`: pin numbers. **Placeholders until confirmed** (`DYNO_PINS_CONFIRMED`).
- `include/Defines/DynoConfig.h`: tunable settings (timeouts, retries, CAN bitrate and freshness, torque/power limits, CSV row interval) and the mechanical setup. **Geometry is a placeholder until confirmed** (`GEOMETRY_CONFIRMED`, `SAME_SHAFT_CONFIRMED`): torque/power stay invalid until then.
- `lib/DynoCore/src/EcuSignals.h`: our copy of the DBC (which ECU signals are decoded and how).
- `test/test_*/`: host test suites. `test/test_device_*/`: on-device tests (need a devkit).
- `docs/`: how each part works, plus hardware checklists. `docs/csv-sample.csv` is an example capture.
- `tools/`: `check_csv.py`, the CSV capture checker.

## Docs

- [Load-cell acquisition (HX711)](docs/loadcell-acquisition.md): timing, library pitfalls, fake inputs, hardware checklist.
- [ECU CAN decoding](docs/can-decoding.md): signals, received vs fresh, driver mode, fake ECU sequence, hardware checklist.
- [Torque and power](docs/torque-power.md): formulas and units, validity rules, freshness/skew limits, sign and calibration-version handling, mechanical setup note, hardware checklist.
- [CSV serial output](docs/csv-output.md): columns, example rows, row interval, serial settings, how to capture and check, dropped rows, hardware checklist.
