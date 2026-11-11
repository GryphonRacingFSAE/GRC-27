# Load-cell acquisition (HX711)

How the dyno reads its load cell (issue #34), why it is built this way, and how to check it on hardware.

## What it does

The **load-cell task** is the only code that talks to the HX711. Every 5 ms it asks the chip "do you have a reading?" If the answer is yes, it reads the value once and stamps it with the current `millis()`. It then publishes the latest reading to a **mailbox** (a one-slot FreeRTOS queue). Other tasks look at the mailbox whenever they need the reading.

```
HX711 ──► Hx711Provider ──► LoadCellAcquirer.step(now) ──► sample mailbox ──► (#35 calibration, #37 maths, …)
           (src/Hardware)     (lib/DynoCore, host-tested)    (xQueuePeek)
```

The published `LoadCellSample` is:

| Field | Meaning |
|---|---|
| `raw` | HX711 counts, exactly as the chip sent them. Can be negative. Not filtered, so out-of-range rejection is #35's job. |
| `timestamp_ms` | `millis()` when it was read. **When it changes, there is a new reading.** |
| `status` | `NoSampleYet` (nothing read since boot), `Valid`, or `Fault` (sensor stopped answering). |

On a `Fault`, `raw` and `timestamp_ms` still hold the **last good** reading (or `0`/`0` if there never was one) for reference. **Never use them as a live value while the status is `Fault`.**

The acquirer also keeps running totals that the monitor printout shows: `timeouts`, `faults` (times a Fault started), `recoveries` (times a good reading ended a Fault) and `samples`.

## Timing

All numbers live in `include/Defines/DynoConfig.h`.

| Setting | Value | Why |
|---|---|---|
| `HX711_POLL_INTERVAL_MS` | 5 ms | Faster than the HX711's quickest rate (80 Hz = a reading every 12.5 ms), so no reading is skipped. A `static_assert` enforces this. |
| `HX711_READY_DEADLINE_MS` | 150 ms | Longer than the slowest rate (10 Hz = every 100 ms), so either RATE-pin wiring works with no false timeouts. |
| `HX711_RETRY_LIMIT` | 3 | One or two missed deadlines are hiccups and the status stays `Valid`. The third in a row means `Fault`. |
| `HX711_RECOVERY_INTERVAL_MS` | 1000 ms | After a Fault the chip is power-cycled and then left alone for 1 s before polling resumes. |

The result is that a sensor which stops answering is reported as **`Fault` within 450 ms** of its last reading (3 × 150 ms). After recovery, the first good reading turns the status back to `Valid` with a fresh timestamp. If the sensor stays unplugged, the cycle repeats: 1 s of rest, then 450 ms of tries, then another power-cycle.

The HX711 runs at **10 Hz or 80 Hz depending on how its RATE pin is wired** on the board. Check the board and note the measured rate in the hardware checklist below. The code works with either.

`millis()` wraps back to 0 after about 49.7 days. All time comparisons use unsigned subtraction (`now - start`), which stays correct across the wrap, and the tests cover it.

## The HX711 library and what we avoid

We use **`bogde/HX711@0.7.5`** (pinned in `platformio.ini`). Several of its functions can **wait forever** if the sensor is unplugged, which would freeze the task:

| Library call | Used? | Why |
|---|---|---|
| `begin()`, `is_ready()`, `power_down()`, `power_up()` | Yes | Return immediately. |
| `read()` | Yes, **only right after `is_ready()` returned true** | Internally it waits until the chip is ready. Since we have already checked, that wait is instant. It disables interrupts for about 50–60 µs while clocking the bits out. |
| `wait_ready()`, `read_average()`, `get_value()`, `get_units()`, `tare()` | **Never** | Each one waits with no time limit. |

Two other details are handled in `src/Hardware/Hx711Provider.cpp`:

- **Pull-up on DOUT.** On ESP chips the library leaves the data pin floating. A floating pin attached to an unplugged sensor can read as LOW, which means "ready", and the code would then read garbage. We turn on the ESP32's internal pull-up after `begin()`. An unplugged sensor then reads HIGH ("not ready"), which leads to timeouts and then `Fault`.
- **Power-cycle.** `power_down()` raises the clock pin, and the chip only switches off after the pin has been high for more than 60 µs. We hold it for 100 µs before `power_up()`. The chip wakes on channel A at gain 128, which is the gain we use, so a power-cycle never changes the gain.

## Running the tests (no hardware)

```
pio test -e native            # all host tests, with AddressSanitizer/UBSan
pio test -e native -f test_loadcell
```

`test/test_loadcell/` drives `LoadCellAcquirer` with `FakeLoadCellProvider`, a scripted sensor, through simulated time. It covers:
- the first reading
- not-ready polls
- never reading without "ready"
- negative counts
- single timeouts
- Fault and power-cycle
- the recovery rest
- recovery
- a mid-stream unplug at 80 Hz
- a sensor unplugged from boot
- long 10 Hz and 80 Hz runs
- `millis()` rollover
- the fake-inputs demo script

## Fake inputs (bare devkit, no HX711)

```
pio run -e esp32-s3-fake -t upload && pio device monitor
```

This builds with `DYNO_FAKE_INPUTS=1`. Instead of the HX711, the load-cell task uses a scripted fake: an 80 Hz ramp of 1000 counts/s from 0, a 3 s "unplug" from 27 s to 30 s, repeating every 30 s. You should see `raw` climb, then `status=Fault` about 0.5 s after the 27 s mark, then `Valid` again from 0 at 30 s. The board prints `# FAKE INPUTS` at boot and every 10 s.

## Serial monitor output (temporary, until #38)

While `DYNO_LOADCELL_MONITOR` is 1 (the default for now), a small task prints the mailbox twice a second:

```
# loadcell raw=84213 t=51230 status=Valid timeouts=0 faults=0 recoveries=0 samples=4098
```

Every 10 s it also repeats the startup warnings (unconfirmed pins, fake inputs), because the USB serial connection often misses boot messages. Lines start with `# ` so the CSV tools from #38 skip them. #38 replaces this task with the real output task.

## Hardware checklist (#34)

Do these in order at the first hardware session and write the results in the PR or the plan.

1. **Pins.** Check `PIN_HX711_DOUT` / `PIN_HX711_SCK` in `include/Defines/pindefs.h` against the GRC27 schematic. Fix them if needed, then set `DYNO_PINS_CONFIRMED` to 1. The `# WARNING` line should disappear.
2. **Wiring.** Load cell to HX711 (E+, E-, A+, A-), HX711 to the board (VCC, GND, DOUT, SCK). Flash with `pio run -t upload && pio device monitor`.
3. **Unloaded readings.** With nothing on the load cell, `status=Valid` and `raw` is steady, wobbling by only a small amount. Note the typical value and how much it wobbles: ______
4. **Gentle load.** Press or hang a small weight. `raw` moves one way and comes back when you remove it. Pull the other way and it moves the other way (it may go negative). Note the direction: ______
5. **Sample rate.** Over 10 s of monitor lines, `samples` should go up by about 100 (10 Hz) or about 800 (80 Hz). Measured: ______ Hz. This tells you how the RATE pin is wired.
6. **Unplug.** Pull the HX711's DOUT wire, or the whole HX711, while running. Within about 0.5 s the status should be `status=Fault`, with `faults` up by 1. The board must not freeze: the monitor lines keep coming.
   - Pulling only the **load-cell** wires (E+/E-/A+/A-) is different. The HX711 keeps converting and reports the reading as `Valid`, but `raw` jumps to a junk or maxed-out value. That is expected for #34. Catching it is #35's job, which rejects maxed-out readings.
7. **Replug.** Reconnect. Within a couple of seconds the status should be back to `status=Valid`, `recoveries` should go up by 1, and `t` should be advancing again. It can take up to about 2.5 s if you replug just as a retry cycle starts: 1 s of rest, then the chip's first conversion.
8. **Soak test.** Leave it running for **30 minutes**. At the end, check that monitor lines are still printing and there were no resets (no boot messages mid-run). Record `timeouts`, `faults`, `recoveries` and `samples`: ______. `faults` should be 0 if nothing was unplugged.
