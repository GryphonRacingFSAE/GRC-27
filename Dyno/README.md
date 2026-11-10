# Gryphon Racing — Dyno

The Dyno board supports Gryphon Racing's dynamometer testing setup by receiving engine RPM data from the MaxxECU over CAN. It makes engine-speed information available within the test setup to support monitoring during engine testing.

## Building and testing

Run these from this folder (`GRC-27/Dyno/`, where `platformio.ini` is):

| Command | What it does | Hardware needed |
|---|---|---|
| `pio test -e native` | Runs the host tests on your computer (Unity, with AddressSanitizer/UBSan) | None |
| `pio run` | Builds the ESP32-S3 firmware | None |
| `pio run -t upload && pio device monitor` | Flashes the board and opens the serial monitor (115200 baud) | Dyno board |

Layout:
- `src/`: ESP32-only code (FreeRTOS tasks, hardware adapters).
- `include/Defines/pindefs.h`: pin numbers. **Placeholders until confirmed** (`DYNO_PINS_CONFIRMED`).
- `include/Defines/DynoConfig.h`: tunable settings (timeouts, retries).
- `test/test_*/`: host test suites.
