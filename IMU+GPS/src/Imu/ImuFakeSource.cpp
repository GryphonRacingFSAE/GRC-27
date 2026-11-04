// The fake IMU and its script. See ImuFakeSource.h.

#include <Imu/ImuFakeSource.h>

// Shortcuts that keep the script readable.
#define SAMPLE(ax, ay, az, gx, gy, gz) {true, {true, true, {ax, ay, az}, {gx, gy, gz}, false, false}}
#define NO_DATA {false, {false, false, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false, false}}

// One line is one 10 ms tick. Accel is in milli-g, gyro is in deg/s.
// Every axis has a different number, so swapped axes show up in the tests.
const FakeImuEntry DEFAULT_FAKE_IMU_SCRIPT[] = {
    // 0: positive on every axis
    SAMPLE(100.0f, 200.0f, 1000.0f, 1.5f, 2.5f, 3.5f),
    // 1: negative on every axis
    SAMPLE(-250.0f, -500.0f, -1000.0f, -10.0f, -20.0f, -30.0f),
    // 2: zero on every axis (a real reading, not "no data")
    SAMPLE(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
    // 3: parked level car
    SAMPLE(0.0f, 0.0f, 1000.0f, 0.0f, 0.0f, 0.0f),
    // 4-15: 120 ms of no data, so the last reading goes stale
    NO_DATA, NO_DATA, NO_DATA, NO_DATA, NO_DATA, NO_DATA,
    NO_DATA, NO_DATA, NO_DATA, NO_DATA, NO_DATA, NO_DATA,
    // 16: both reads failed
    {true, {false, false, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false, false}},
    // 17: accel worked, gyro failed
    {true, {true, false, {10.0f, -20.0f, 990.0f}, {0.0f, 0.0f, 0.0f}, false, false}},
    // 18: full recovery
    SAMPLE(50.0f, -30.0f, 1010.0f, 0.5f, -0.5f, 0.25f),
};

const uint32_t DEFAULT_FAKE_IMU_SCRIPT_LENGTH = sizeof(DEFAULT_FAKE_IMU_SCRIPT) / sizeof(DEFAULT_FAKE_IMU_SCRIPT[0]);

#undef SAMPLE
#undef NO_DATA

void initFakeImuSource(FakeImuSource& source, const FakeImuEntry* script, uint32_t length) {
    source.script = script;
    source.length = length;
    source.index = 0;
}

bool pollFakeImu(FakeImuSource& source, RawImuSample& out) {
    if (source.script == nullptr || source.length == 0) {
        return false;
    }

    const FakeImuEntry& entry = source.script[source.index];
    source.index = (source.index + 1) % source.length;  // goes back to 0 at the end

    if (!entry.hasData) {
        return false;
    }
    out = entry.raw;
    return true;
}
