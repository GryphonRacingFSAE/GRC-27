# IMU reader (issue #41)

Reads acceleration and rotation rate from the IMU and shares them with CAN (#43) and serial diagnostics (#44).

**Status:** the laptop tests pass and the firmware builds, but none of this has run on a real sensor yet.

## What the numbers mean

- **Acceleration is in g and includes gravity.** A level, stationary car reads about +1 g on the vertical axis. Nothing is subtracted to remove gravity.
- **Rotation rate is in degrees per second.** It is how fast the car is turning, not an angle.
- There is no orientation fusion, no Euler angles, no magnetometer output and no GPS correction.
- **Valid** means a reading was acquired and passed its checks. **Fresh** means it is also 100 ms old or newer. Check `isFresh()`, because a valid reading can still be old.
- Zero and negative values are real measurements. "No data" is shown by `valid = false` and NaN values.
- A failed read keeps the old numbers and their original timestamp.
- A saturated reading (the sensor hit its limit) is not published: it is marked `Saturated` and the old value is kept.

## Vehicle axes

The axes are right-handed, and positive rotation follows the right-hand rule.

| Axis | Positive direction | Rotation about it | Positive rotation |
| --- | --- | --- | --- |
| Longitudinal | Forward | Roll | Right side down |
| Lateral | Left | Pitch | Nose down |
| Vertical | Up | Yaw | Turning left |

## Mounting table

**This is a placeholder.** The real mounting has not been measured. A wrong table still gives believable numbers on the wrong axis or with the wrong sign, and only tilting and rotating the real board will catch that. The table is `PLACEHOLDER_IMU_MOUNTING` in `ImuConversion.h`.

| Vehicle axis | Sensor axis |
| --- | --- |
| Longitudinal | +X (unconfirmed) |
| Lateral | +Y (unconfirmed) |
| Vertical | +Z (unconfirmed) |

## Settings

The pins are in `include/Defines/pindefs.h`.

| Setting | Value | Confirmed? |
| --- | --- | --- |
| Sensor | SparkFun ICM-20948, library v1.3.2 | Assumed, Riyan to confirm |
| Bus | SPI at 4 MHz | Assumed, Riyan to confirm |
| Chip select | GPIO 10 | Given, may change |
| MOSI / SCK / MISO | GPIO 11 / 12 / 13 | Assumed, Riyan to confirm |
| Accel range | ±8 g | Agreed |
| Gyro range | ±500 deg/s | Agreed |
| Sample rate | Gyro 100 Hz, accel about 102.3 Hz (100 Hz exactly is not possible) | Agreed |
| Filter | About 24 Hz low-pass | Not agreed yet |
| Freshness limit | 100 ms | Agreed, check with #43 |
| Start-up retry | At most once per second | Chosen in #41 |
| Task loop | Every 5 ms | Chosen in #41 |
| Task priority and stack | 1 and 4096 bytes | Estimates, not measured |

## How the code fits together

```text
ImuIcm20948 (real)  or  ImuFakeSource (test)
        |  RawImuSample
        v
ImuRunner  ->  ImuPipeline  ->  ImuConversion + ImuQuality
        |  ImuDiagnostics (values, status, counts)
        v
ImuTask: one lock-protected latest value  ->  imuGetLatest()  ->  #43 and #44
```

Other tasks get the data like this:

```cpp
#include <Imu/ImuTask.h>
#include <Imu/ImuQuality.h>

ImuDiagnostics imu;
if (imuGetLatest(imu)) {
    uint32_t now = millis();  // read this AFTER the copy
    if (isFresh(imu.latest.accel, now)) {
        // use imu.latest.accel
    }
}
```

**Why a latest value instead of a queue:** CAN and serial only want the newest reading. There is no queue to fill, so a slow reader can't hold up the IMU. The lock is held only while copying, and every wait on it is limited to 2 ms.

**Why a missing IMU can't stop other tasks:** `imuTaskStart()` returns without touching the sensor. Start-up happens inside the IMU task, at most once per second, and that task sleeps on every loop.

## Fake source

The real sensor is the default. The fake runs only when the firmware is built with `-DIMU_USE_FAKE_SOURCE`, which also prints a compiler warning. Nothing in `platformio.ini` sets this flag.

## Reproducing the results

Laptop tests, from `IMU+GPS` in WSL:

```bash
SRC="src/Imu/ImuConversion.cpp src/Imu/ImuDiagnostics.cpp src/Imu/ImuFakeSource.cpp src/Imu/ImuPipeline.cpp src/Imu/ImuQuality.cpp src/Imu/ImuRunner.cpp"
for t in test/host/*.cpp; do
    g++ -std=c++17 -Wall -Wextra -Iinclude "$t" $SRC -o /tmp/imu_test && /tmp/imu_test | tail -1
done
```

Firmware build, from `IMU+GPS` in PowerShell:

```powershell
pio run
```

Debug build (replace `COM5` with your port). It prints one line of IMU values every 200 ms:

```powershell
$env:PLATFORMIO_BUILD_FLAGS = "-DIMU_DEBUG_PRINT"
pio run -t upload --upload-port COM5
Remove-Item Env:PLATFORMIO_BUILD_FLAGS
pio device monitor --port COM5
```

To test without a sensor, use `"-DIMU_USE_FAKE_SOURCE -DIMU_DEBUG_PRINT"` as the flags. Never put that build on the car.

## Troubleshooting

- **If there is no data:** check the `init` state, then the wiring and pins.
- **If values are off by 1,000:** check mg versus g.
- **If directions are wrong:** fix the mounting table, not the driver.

## Open items for Riyan (#40)

- Confirm the sensor, the bus and all the pins.
- Confirm the task priority, the stack size and the shared latest value.
- Decide whether `ImuTypes.h` moves into `API.h`.
