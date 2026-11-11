# Gryphon Racing — Dyno

The Dyno board supports Gryphon Racing's dynamometer testing setup by receiving engine RPM data from the MaxxECU over CAN. It makes engine-speed information available within the test setup to support monitoring during engine testing.

## Building and testing

Run these from this folder (`GRC-27/Dyno/`, where `platformio.ini` is):

| Command | What it does | Hardware needed |
|---|---|---|
| `pio test -e native` | Runs the host tests on your computer (Unity, with AddressSanitizer/UBSan) | None |
| `pio run` | Builds the ESP32-S3 firmware | None |
| `pio run -t upload && pio device monitor` | Flashes the board and opens the serial monitor (115200 baud) | Dyno board |
| `pio run -e esp32-s3-fake -t upload && pio device monitor` | Same, but with a simulated load cell and ECU (`DYNO_FAKE_INPUTS=1`) | Any ESP32-S3 devkit |
| `pio test -e esp32-s3-devboard -f test_device_can_loopback` | On-device CAN self-test of the real driver (add `--without-uploading --without-testing` to only build it) | ESP32-S3 devkit + 1 jumper wire |

Layout:
- `lib/DynoCore/`: hardware-free logic (load-cell acquisition, ECU CAN decoding and reception, fakes). Builds for both the ESP32 and the host tests.
- `lib/DynoHardware/`: ESP32-only adapters (HX711, TWAI CAN) behind DynoCore's interfaces.
- `src/`: ESP32-only code (FreeRTOS tasks).
- `include/Defines/pindefs.h`: pin numbers. **Placeholders until confirmed** (`DYNO_PINS_CONFIRMED`).
- `include/Defines/DynoConfig.h`: tunable settings (timeouts, retries, CAN bitrate and freshness).
- `lib/DynoCore/src/EcuSignals.h`: our copy of the DBC (which ECU signals are decoded and how).
- `test/test_*/`: host test suites. `test/test_device_*/`: on-device tests (need a devkit).
- `docs/`: how each part works, plus hardware checklists.

## Docs

- [Load-cell acquisition (HX711)](docs/loadcell-acquisition.md): timing, library pitfalls, fake inputs, hardware checklist.
- [ECU CAN decoding](docs/can-decoding.md): signals, received vs fresh, driver mode, fake ECU sequence, hardware checklist.
