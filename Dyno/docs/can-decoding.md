# ECU CAN decoding (#33)

## What it does

The **CAN task** listens to the MaxxECU on the CAN bus. It turns 7 of the ECU's messages into 13 named measurements (RPM, lambdas, fuel, knock, rev limit, temperatures, battery, gear). It publishes them to a **mailbox** (a one-slot FreeRTOS queue) that other tasks read whenever they need them.

```
ECU ──CAN──► transceiver ──► TwaiCanBus ──► CanReceiver.step(now) ──► EcuState mailbox ──► (#37 power, #38 CSV)
                            (lib/DynoHardware) (lib/DynoCore, host-tested)  CanStatus mailbox ──► (monitor, #38)
                                                    │
                                              decodeEcuFrame + EcuSignals.h table
```

| Piece | Where | Job |
|---|---|---|
| `EcuSignals.h` | `lib/DynoCore/src/` | **Our copy of the DBC.** One table row per signal. |
| `decodeEcuFrame()` | `lib/DynoCore/src/EcuDecoder.*` | Checks one frame and decodes it into `EcuState`. Pure: no hardware, no clock. |
| `EcuState` | `lib/DynoCore/src/EcuState.h` | Raw values, plus a received flag and `last_rx_ms` for each message. Also `freshMask()` / `receivedMask()`. |
| `CanReceiver` | `lib/DynoCore/src/CanReceiver.*` | Starts the driver (and retries), receives with a bounded wait, decodes, handles bus-off, keeps counters. |
| `TwaiCanBus` | `lib/DynoHardware/src/` | The ESP32-S3 CAN controller behind the `CanBus` interface. |
| `FakeCanBus` | `lib/DynoCore/src/` | A scripted fake ECU for tests, fake-input builds and #38. |
| `CanTask` | `src/Tasks/CanTask.cpp` | Thin FreeRTOS loop around `step()`. |

## The signals

Source: `GRC-27/Telemetry/GRC27.dbc`, **last changed in commit `7faab7e` (2026-09-23)**. They also match MaxxECU's own raw DBC (GRC26 `Dash/MAXXECU RAW DBC.dbc`). All signals are little-endian with zero offset, and **physical value = raw × scale**.

| CSV mask bit | ID | Signal | Start / length | Raw type | Scale | Unit |
|---|---|---|---|---|---|---|
| 0 | 0x520 | EngineSpeed | 0 / 16 | u16 | 1 | rpm |
| 0 | 0x520 | Lambda_Average | 48 / 16 | u16 | 0.001 | lambda |
| 1 | 0x521 | Lambda_A | 0 / 16 | u16 | 0.001 | lambda |
| 1 | 0x521 | Lambda_B | 16 / 16 | u16 | 0.001 | lambda |
| 2 | 0x522 | FuelInjPulsewidth | 0 / 16 | u16 | 0.01 | ms |
| 2 | 0x522 | FuelInjDuty | 16 / 16 | u16 | 0.1 | % |
| 3 | 0x526 | Knock_detected | 7 / 1 | bit | 1 | bool |
| 3 | 0x526 | RevLimit_RPM | 32 / 16 | u16 | 1 | rpm |
| 4 | 0x527 | Lambda_Target | 48 / 16 | **i16** | 0.001 | lambda |
| 5 | 0x530 | ECU_BatteryVoltage | 0 / 16 | u16 | 0.01 | V |
| 5 | 0x530 | IntakeAirTemp | 32 / 16 | u16 | 0.1 | degC |
| 5 | 0x530 | CoolantTemp | 48 / 16 | u16 | 0.1 | degC |
| 6 | 0x536 | GearPosn | 0 / 16 | u16 | 1 | raw code |

**Not decoded** (on purpose): ThrottlePos, VehicleSpeed, Brake/Clutch pedal, User_Channel_1, and everything else in those frames.

Notes:
- **Raw values are stored exactly as sent** (as `int32_t`): RPM 60000 stays 60000. GRC26 stored it as a signed 16-bit number, so anything above 32767 came out negative.
- **The temperatures are unsigned in the DBC**, so raw `FF FF` reads 6553.5 °C, not −0.1 °C. If the ECU ever reports sub-zero temperatures this way, the DBC must change first; then change the table row.

### Changing the signals when the DBC changes

Edit only `lib/DynoCore/src/EcuSignals.h`:
- **To change a signal:** edit its row.
- **To add a signal:** add it to `EcuSignal` (before `Count`) and add a row.
- **To add a message:** add it to `EcuMessage` and `kEcuMessages`.

Compile-time checks catch rows out of order, duplicate IDs and signals that don't fit in 8 bytes. The host test `test_signal_table_matches_grc27_dbc` **reads GRC27.dbc** and fails if any row differs from it (ID, start bit, length, byte order, sign, scale, offset).

## Example decoded frames

These are all host tests in `test/test_can/` (bytes are in bus order, byte 0 first):

| ID | Payload | Result |
|---|---|---|
| 0x520 | `70 17 00 00 00 00 E8 03` | 6000 rpm, lambda 1.000 (the issue's reference frame) |
| 0x520 | `60 EA 00 00 00 00 00 00` | 60000 rpm (not negative) |
| 0x527 | `00 00 00 00 00 00 18 FC` | target lambda −1.000 |
| 0x526 | `80 00 00 00 00 00 00 00` | knock = 1 (`7F` → 0; brake/clutch bits are ignored) |
| 0x530 | `2E 05 FF FF 5A 01 75 03` | 13.26 V, IAT 34.6 °C, coolant 88.5 °C |
| any of the 7 | `00 00 00 00 00 00 00 00` | received, all values 0 (a real zero) |

## Which frames are rejected

`decodeEcuFrame()` checks everything before it touches the state. A rejected frame changes **no** value and **no** receipt time:

| Frame | Result | Counted as |
|---|---|---|
| Extended (29-bit) ID | `Extended` | ignored (and listed in `unknown=`) |
| Remote request | `Remote` | ignored |
| ID not in the table | `UnknownId` | ignored (and listed in `unknown=`) |
| Our ID, length ≠ 8 | `BadLength` | malformed |
| Our ID, no payload | `NullPayload` | malformed |

## Received vs fresh

Each of the 7 messages has its own **received** flag and **`last_rx_ms`**. Receiving 0x520 says nothing about 0x530.

- **Received** means at least one valid frame of that message has arrived since boot. Before that, its values are 0 and **must not be used**: "never received" is different from "received a real zero".
- **Fresh** means received, **and** the latest frame is less than `CAN_FRESHNESS_MS` (2000 ms) old. A message that stops arriving keeps its last values and stays "received", but stops being fresh 2000 ms after its last frame. Nothing is zeroed or made up.

`EcuState::receivedMask()` and `freshMask(now_ms, CAN_FRESHNESS_MS)` give these as 7-bit masks in CSV order (bit 0 = 0x520 … bit 6 = 0x536). Age is `now_ms - last_rx_ms` in unsigned arithmetic, so it stays correct when `millis()` wraps after ~49.7 days (tested). **Take `now_ms` after copying the state out of the mailbox**, otherwise a frame that arrived in between looks ~49 days old.

**Freshness threshold and expected ECU update rates:**

- **Threshold:** `CAN_FRESHNESS_MS` = 2000 ms, the team's existing convention for this ECU (GRC26 RTU).
- **Expected rates: not known yet.** Neither the DBC, nor GRC26, nor any team doc records how often the MaxxECU sends these messages. The rate is set per message in the ECU's own CAN output configuration.
- **Our assumption:** every one of the 7 messages arrives at **10 Hz or faster** (every 100 ms or less), which is typical for engine data.

Under that assumption, 2000 ms means about **20 frames in a row must be missed** before a message reads stale. One dropped frame never blanks a value, and a dead bus is still noticed within 2 s.

| If the measured rate of the slowest message is… | Then |
|---|---|
| ≥ 10 Hz (as assumed) | keep 2000 ms, or lower it to about 5 periods if you want faster stale detection |
| between 0.5 and 10 Hz | 2000 ms still works but catches fewer missed frames; consider about 5 periods |
| < 0.5 Hz (slower than one every 2 s) | **2000 ms is too short**: that message would flicker stale. Raise `CAN_FRESHNESS_MS` to about 5 periods |

Measure the real rates at the first hardware session (checklist step 6, the monitor's `hz=` field) and write them here.

## Driver setup and choices

- **API:** the installed framework is Arduino-ESP32 on **ESP-IDF 5.5**, and we use its `driver/twai.h` (the "legacy" TWAI driver, still the one Arduino ships).
- **Bit rate:** 500 kbit/s (`CAN_BITRATE_KBPS`), the same as GRC26's Dyno and GRC26 `TELEMETRY.md` on the same MaxxECU.
- **Mode: `TWAI_MODE_NORMAL`.** In normal mode the controller **acknowledges** frames even though the dyno never sends any data frames. That matters on the dyno bench: if the ECU is the only other device on the bus, someone has to ACK its frames, or it sees errors on every frame.
  - **Downside:** a node in normal mode also *joins in on errors*. If the dyno is misconfigured (wrong bit rate) or miswired, it sends error frames onto the ECU's bus, which can disturb the ECU's other traffic until the dyno's error counters push it to error passive or bus-off. So **check the bit rate before connecting to a bus that matters** (checklist step 0 and step 4's `bus_errors`).
  - **The alternative**, listen-only mode, never ACKs and never sends errors. Use it only if the bus already has another node acknowledging and you want the dyno to be completely passive. To switch, change the `TwaiCanBus` constructor's mode in `CanTask.cpp`.
- **Filter:** the hardware accepts everything and the decoder filters in software. That way unexpected IDs show up in the `unknown=` list instead of disappearing.
- **Receive queue:** 32 frames (`CAN_RX_QUEUE_LEN`). If it ever fills up, the lost frames show as `missed=`.
- **No busy loops and no waiting forever:**
  - each `twai_receive` waits at most `CAN_RX_TIMEOUT_MS` (20 ms)
  - after a step that got nothing, the task sleeps `CAN_IDLE_DELAY_MS` (10 ms)
  - while frames keep arriving, it loops straight away to empty the queue
- **Errors:**
  - Every driver install/start result is checked. On failure, `link=StartFailed`, and it retries every `CAN_RESTART_INTERVAL_MS` (1 s).
  - Bus-off triggers `twai_initiate_recovery()` (`link=Recovering`), then `twai_start()` again when recovery finishes. If the driver refuses the recovery request, it is counted (`recovery_failures=`) and asked again every `CAN_RESTART_INTERVAL_MS`, so it never gets stuck.
  - The controller's own error counters are reported every loop, whatever the link state: `bus_errors` (total bus errors seen), `rec`/`tec` (receive/transmit error counters, 0–255) and `ERROR_PASSIVE` once either reaches 128. **A wrong bit rate or bad wiring shows as `bus_errors` climbing with `decoded` stuck at 0.** A silent ECU shows everything flat at 0.
  - Since the dyno never transmits, bus-off is unlikely (it is driven by *transmit* errors), but it is handled anyway.
  - Values are never zeroed during any of this. They simply stop being fresh.

## Fake ECU (for #38, and for a bare devkit)

`demoEcuScript()` sends all 7 messages at 50 Hz on a 25 s cycle that starts at boot and repeats:

| Time in cycle | ECU sends | What you see |
|---|---|---|
| 0–2 s | nothing | **startup**: `recv=0x00 fresh=0x00`, all values `-` |
| 2–10 s | all 7 | **normal updates**: `0x7F / 0x7F`, RPM ramps up |
| 10–15 s | all except 0x530 | **missing frames**: 0x530 goes **stale** at ≈12 s → `fresh=0x5F` |
| 15–20 s | nothing | **everything stale** at ≈17 s → `fresh=0x00`, still `recv=0x7F` |
| 20–25 s | all 7 | **recovery**: `fresh=0x7F` again |

The values are RPM = 1000 + t/2 (t = ms into the cycle), lambda 1.000 / 0.990 / 1.010, target 0.950, 11.00 ms, 45.0 %, knock 0, rev limit 11000, 13.26 V, IAT 34.6 °C, coolant 88.5 °C and gear 1. The exact timeline is pinned down by `test_demo_sequence_timeline` in `test/test_can_receiver/`.

**For #38:** the same fake works on the host with a fake clock:

```cpp
FakeCanBus bus(demoEcuScript());
CanReceiver rx(bus, {CAN_RX_TIMEOUT_MS, CAN_RESTART_INTERVAL_MS});
for (uint32_t t = 0; t < 30000; t += 5) {
    bus.setTime(t);
    while (rx.step(t) != CanStepResult::Idle) {}
    // rx.ecu(), rx.ecu().freshMask(t, CAN_FRESHNESS_MS), rx.status() ...
}
```

**On a bare devkit:**

```
pio run -e esp32-s3-fake -t upload && pio device monitor
```

The board says `# FAKE INPUTS: load cell and ECU CAN are simulated` at boot and every 10 s.

`FakeCanBus` can also inject any frame (`inject()`) and simulate faults: `failNextStarts()`, `failNextReceives()`, `goBusOff()` / `refuseNextRecoveries()` / `finishRecovery()` and `setErrors()` (missed frames and controller error counters).

## Serial monitor output (temporary, until #38)

While `DYNO_SERIAL_MONITOR` is 1, `MonitorTask` prints this twice a second, alongside the `# loadcell` line:

```
# ecu recv=0x7F fresh=0x7F EngineSpeed=6000 Lambda_Average=1.000 Lambda_A=0.990 ... GearPosn=1
# can link=Running decoded=4550 ignored=0 malformed=0 rx_errors=0 missed=0 bus_errors=0 rec=0 tec=0 bus_off=0 recoveries=0 recovery_failures=0 start_failures=0 hz=50,50,50,50,50,50,50 unknown=none
```

- `-` means that message was never received.
- `hz=` is the measured frames per second of each message, in mask order (0x520 … 0x536).
- `unknown=` lists up to 8 distinct IDs that aren't ours (`x…` = extended ID, `+more` = more than 8 seen).
- `bus_errors`/`rec`/`tec` are the controller's own error counters; ` ERROR_PASSIVE` appears after `tec=` once either counter reaches 128.

## Tests

| Command | What | Hardware |
|---|---|---|
| `pio test -e native -f test_can` | Decoder + `EcuState` + DBC cross-check (31 tests) | none |
| `pio test -e native -f test_can_receiver` | `CanReceiver` with `FakeCanBus`, plus the demo sequence (21 tests) | none |
| `pio test -e native` | Everything on the host (70 tests, with AddressSanitizer/UBSan) | none |
| `pio test -e esp32-s3-devboard -f test_device_can_loopback` | Real `TwaiCanBus` + decoder + receiver in self-test mode (6 tests) | devkit + 1 jumper wire |

The host tests cover:
- every row of the issue's "Checks to implement" table
- a typical value and the raw limits for all 13 signals, plus byte order
- rejection with no state change
- startup, freshness that expires independently per message, and `millis()` rollover
- start failure and retry
- bounded waits
- receive errors and queue overflow
- bus-off recovery, including a refused recovery request
- the controller's error counters and error-passive state, kept up to date in every link state
- the unknown-ID list
- the fake sequence

## Hardware checklist (#33)

Fake frames don't prove wiring, bus timing or ECU configuration. Real ECU reception is **pending hardware validation** and is carried into #39. Do these in order and write the results in the PR or the plan.

0. **Loopback, no ECU needed.** On a bare ESP32-S3 devkit, put a jumper wire from the `PIN_CAN_TX` GPIO to the `PIN_CAN_RX` GPIO (numbers in `include/Defines/pindefs.h`) and run `pio test -e esp32-s3-devboard -f test_device_can_loopback`. All 6 tests should pass. This proves the driver, bit timing and frame translation work before you spend time on the bench. Result: ______
1. **Pins.** Confirm `PIN_CAN_TX`/`PIN_CAN_RX` against the Dyno board schematic (they connect to the transceiver's TXD/RXD), fix `include/Defines/pindefs.h` if needed, and set `DYNO_PINS_CONFIRMED` once every pin is checked.
2. **Termination.** With everything powered off, measure between CANH and CANL. It should read about **60 Ω** (a 120 Ω terminator at each end of the bus). Measured: ______ Ω
3. **ECU off.** Flash with `pio run -t upload && pio device monitor`. Expect `# can link=Running` and `start_failures=0`. Expect `# ecu recv=0x00 fresh=0x00` with every value `-`. The `# loadcell` lines must keep printing.
4. **ECU on.** Within about a second, expect `recv=0x7F fresh=0x7F`. If not:
   - **`decoded` stays 0 but `ignored` goes up:** the ECU sends different IDs. Compare `unknown=` with the DBC. An `x` prefix means the ECU sends extended IDs.
   - **`malformed` goes up:** the ECU's frame lengths differ from 8.
   - **`bus_errors` climbs (and `rec` rises, maybe `ERROR_PASSIVE`) but `decoded` stays 0:** the dyno can see the bus but can't read it. That's a wrong bit rate (check the ECU's CAN speed against `CAN_BITRATE_KBPS`), CANH/CANL swapped, or missing termination. Unplug the dyno while you fix it, because in normal mode it puts error frames on the bus (see "Driver setup and choices").
   - **Nothing increases at all (all counters flat at 0):** no signal is reaching the controller. Check the wiring, the transceiver's power/standby pin, and that the ECU's CAN output is enabled in its software.
5. **Values.** Compare against the ECU's own display or tuning software:
   - idle RPM
   - coolant and intake temperature
   - battery voltage, against a multimeter
   - lambda
   - gear

   Write down any value that is off by a factor of 10/100 (scale wrong) or wildly wrong (byte order or start bit wrong).
6. **Rates.** Record the `hz=` values for each message: ______. If the slowest is much faster than 2.5 Hz, consider lowering `CAN_FRESHNESS_MS` (≈ 5× the slowest period).
7. **Unplug CAN** while running. Within 2 s, expect `fresh=0x00` while `recv=0x7F` stays. The values stay at their last reading. `link` stays `Running`, and the load-cell lines keep coming (nothing freezes). **Replug:** `fresh=0x7F` again within a second.
8. **Soak.** Run for 30 minutes with the ECU on. At the end, check:
   - `missed=0`
   - `rx_errors`, `bus_errors` and `bus_off` haven't grown (a few `bus_errors` from plugging and unplugging are fine)
   - `rec`/`tec` are back near 0
   - there were no resets

Notes for the PR, and what is still pending:
- The DBC revision is `7faab7e`.
- Real ECU reception, the ECU's message rates and the pin numbers are **pending hardware**.
