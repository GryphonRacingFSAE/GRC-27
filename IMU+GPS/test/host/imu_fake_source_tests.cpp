/* Laptop tests for the fake IMU and the shared pipeline. Plays the fake script
 * through the same path the real sensor uses and checks each step.
 *
 * Build and run in WSL from the IMU+GPS folder:
 *   g++ -std=c++17 -Wall -Wextra -Iinclude test/host/imu_fake_source_tests.cpp src/Imu/ImuFakeSource.cpp src/Imu/ImuPipeline.cpp src/Imu/ImuQuality.cpp src/Imu/ImuConversion.cpp -o imu_fake_source_tests
 *   ./imu_fake_source_tests
 *
 * NOTE: checkFloat and checkTrue are copied across the test files. They could
 * move into one shared test header.
 */

#include <Imu/ImuFakeSource.h>
#include <Imu/ImuPipeline.h>
#include <Imu/ImuQuality.h>
#include <math.h>
#include <stdio.h>

// Score counters and helpers (same as in the other test files).
static int passCount = 0;
static int failCount = 0;

static const float TOLERANCE = 0.000001f;

static void checkFloat(const char* name, float actual, float expected) {
    bool ok = fabsf(actual - expected) <= TOLERANCE;
    printf("%s  %-52s expected %8.3f  got %8.3f\n", ok ? "PASS" : "FAIL", name, expected, actual);
    if (ok) {
        passCount++;
    } else {
        failCount++;
    }
}

static void checkTrue(const char* name, bool condition) {
    printf("%s  %s\n", condition ? "PASS" : "FAIL", name);
    if (condition) {
        passCount++;
    } else {
        failCount++;
    }
}

// runTick: what the IMU task will do once per period - ask the source for a
// sample and, only if a read was attempted, push it through the pipeline.
static void runTick(FakeImuSource& fake, ImuSnapshot& snap, const AxisMapping& mapping, uint32_t now_ms) {
    RawImuSample raw;
    if (pollFakeImu(fake, raw)) {
        applyRawSample(snap, raw, mapping, now_ms);
    }
}

// Test group 1: play the whole default script with the placeholder table
// (identity: chip X/Y/Z = forward/left/up), so expected values are easy.
// Tick i happens at time 1000 + 10*i ms.
static void testDefaultScript() {
    printf("\n-- default fake script --\n");
    checkTrue("script has 19 lines", DEFAULT_FAKE_IMU_SCRIPT_LENGTH == 19);

    FakeImuSource fake;
    initFakeImuSource(fake, DEFAULT_FAKE_IMU_SCRIPT, DEFAULT_FAKE_IMU_SCRIPT_LENGTH);
    ImuSnapshot snap = makeUnavailableImuSnapshot();
    const AxisMapping& map = PLACEHOLDER_IMU_MOUNTING;

    // Tick 0 (t=1000): positive. 100 mg -> 0.1 g etc.; gyro unchanged.
    runTick(fake, snap, map, 1000);
    checkFloat("t0 positive: longitudinal 100 mg -> 0.1 g", snap.accel.longitudinal_g, 0.1f);
    checkFloat("t0 positive: lateral 200 mg -> 0.2 g", snap.accel.lateral_g, 0.2f);
    checkFloat("t0 positive: vertical 1000 mg -> 1.0 g", snap.accel.vertical_g, 1.0f);
    checkFloat("t0 positive: roll 1.5 dps (no conversion)", snap.gyro.rollRate_dps, 1.5f);
    checkFloat("t0 positive: pitch 2.5 dps", snap.gyro.pitchRate_dps, 2.5f);
    checkFloat("t0 positive: yaw 3.5 dps", snap.gyro.yawRate_dps, 3.5f);
    checkTrue("t0: accel + gyro valid, timestamp 1000",
              snap.accel.valid && snap.gyro.valid && snap.accel.timestamp_ms == 1000 && snap.gyro.timestamp_ms == 1000);

    // Tick 1 (t=1010): negative.
    runTick(fake, snap, map, 1010);
    checkFloat("t1 negative: longitudinal -250 mg -> -0.25 g", snap.accel.longitudinal_g, -0.25f);
    checkFloat("t1 negative: lateral -500 mg -> -0.5 g", snap.accel.lateral_g, -0.5f);
    checkFloat("t1 negative: vertical -1000 mg -> -1.0 g", snap.accel.vertical_g, -1.0f);
    checkFloat("t1 negative: yaw -30 dps", snap.gyro.yawRate_dps, -30.0f);

    // Tick 2 (t=1020): zero is a real, valid measurement.
    runTick(fake, snap, map, 1020);
    checkFloat("t2 zero: vertical 0 g", snap.accel.vertical_g, 0.0f);
    checkTrue("t2 zero: still valid + Ok", snap.accel.valid && snap.accel.status == ImuStatus::Ok);

    // Tick 3 (t=1030): parked level car.
    runTick(fake, snap, map, 1030);
    checkFloat("t3 stationary: vertical +1 g", snap.accel.vertical_g, 1.0f);

    // Ticks 4-15 (t=1040..1150): no data. Values and time must not change.
    for (uint32_t t = 1040; t <= 1150; t += 10) {
        runTick(fake, snap, map, t);
        if (t == 1130) {
            checkTrue("gap: fresh at 1130 (age exactly 100 ms)", isFresh(snap.accel, t) && isFresh(snap.gyro, t));
        }
        if (t == 1140) {
            checkTrue("gap: stale at 1140 (age 110 ms)", !isFresh(snap.accel, t) && !isFresh(snap.gyro, t));
        }
    }
    checkTrue("gap: timestamp still 1030 (old data, old time)", snap.accel.timestamp_ms == 1030);
    checkTrue("gap: still valid (stale, not broken)", snap.accel.valid);

    // Tick 16 (t=1160): read attempted but failed for both parts.
    runTick(fake, snap, map, 1160);
    checkTrue("t16 failed read: both ReadFailed + not valid",
              snap.accel.status == ImuStatus::ReadFailed && snap.gyro.status == ImuStatus::ReadFailed &&
              !snap.accel.valid && !snap.gyro.valid);
    checkTrue("t16: old timestamp 1030 kept", snap.accel.timestamp_ms == 1030 && snap.gyro.timestamp_ms == 1030);
    checkFloat("t16: old vertical 1 g kept for diagnostics", snap.accel.vertical_g, 1.0f);

    // Tick 17 (t=1170): accel fine, gyro failed.
    runTick(fake, snap, map, 1170);
    checkTrue("t17: accel valid + fresh at 1170", isFresh(snap.accel, 1170));
    checkFloat("t17: accel vertical 990 mg -> 0.99 g", snap.accel.vertical_g, 0.99f);
    checkTrue("t17: gyro still ReadFailed, timestamp 1030",
              snap.gyro.status == ImuStatus::ReadFailed && snap.gyro.timestamp_ms == 1030);

    // Tick 18 (t=1180): full recovery.
    runTick(fake, snap, map, 1180);
    checkTrue("t18 recovery: both fresh", isFresh(snap.accel, 1180) && isFresh(snap.gyro, 1180));
    checkTrue("t18: gyro timestamp 1180", snap.gyro.timestamp_ms == 1180);
    checkFloat("t18: gyro yaw 0.25 dps", snap.gyro.yawRate_dps, 0.25f);

    // Tick 19 (t=1190): script repeats from line 0.
    runTick(fake, snap, map, 1190);
    checkFloat("t19 repeat: back to line 0 (0.1 g)", snap.accel.longitudinal_g, 0.1f);
}

// Test group 2: the pipeline really uses the mounting table (a rotated one,
// same as in imu_conversion_tests.cpp): longitudinal = -Z, lateral = +X,
// vertical = -Y.
static void testPipelineMapping() {
    printf("\n-- applyRawSample with a rotated table --\n");
    const AxisMapping rotated = {{SensorAxis::Z, -1}, {SensorAxis::X, +1}, {SensorAxis::Y, -1}};
    ImuSnapshot snap = makeUnavailableImuSnapshot();

    RawImuSample raw = {true, true, {1000.0f, 2000.0f, 3000.0f}, {10.0f, -20.0f, 30.0f}, false, false};
    applyRawSample(snap, raw, rotated, 500);
    checkFloat("accel longitudinal = -Z = -3 g", snap.accel.longitudinal_g, -3.0f);
    checkFloat("accel lateral = +X = 1 g", snap.accel.lateral_g, 1.0f);
    checkFloat("accel vertical = -Y = -2 g", snap.accel.vertical_g, -2.0f);
    checkFloat("gyro roll = -Z = -30 dps", snap.gyro.rollRate_dps, -30.0f);
    checkFloat("gyro pitch = +X = 10 dps", snap.gyro.pitchRate_dps, 10.0f);
    checkFloat("gyro yaw = -Y = 20 dps", snap.gyro.yawRate_dps, 20.0f);

    // A NaN from the driver must be rejected by the pipeline, not published.
    RawImuSample bad = {true, true, {NAN, 0.0f, 1000.0f}, {0.0f, 0.0f, 0.0f}, false, false};
    applyRawSample(snap, bad, rotated, 510);
    checkTrue("NaN accel -> InvalidValue, old timestamp 500 kept",
              snap.accel.status == ImuStatus::InvalidValue && snap.accel.timestamp_ms == 500);
    checkTrue("gyro in the same sample still accepted", snap.gyro.valid && snap.gyro.timestamp_ms == 510);

    // Gyro maxed out: not published, old value and old time kept; accel fine.
    RawImuSample spin = {true, true, {0.0f, 0.0f, 1000.0f}, {500.0f, 0.0f, 0.0f}, false, true};
    applyRawSample(snap, spin, rotated, 520);
    checkTrue("saturated gyro -> Saturated, not valid, timestamp 510 kept",
              snap.gyro.status == ImuStatus::Saturated && !snap.gyro.valid && snap.gyro.timestamp_ms == 510);
    // The saturated sample would put +X = 500 into pitch; the old 0 must stay.
    checkFloat("saturated gyro: old pitch 0 kept (not 500)", snap.gyro.pitchRate_dps, 0.0f);
    checkTrue("accel in the same sample accepted", snap.accel.valid && snap.accel.timestamp_ms == 520);

    // Accel maxed out (e.g. a big kerb hit).
    RawImuSample kerb = {true, true, {0.0f, 0.0f, 8000.0f}, {0.0f, 0.0f, 0.0f}, true, false};
    applyRawSample(snap, kerb, rotated, 530);
    checkTrue("saturated accel -> Saturated, timestamp 520 kept",
              snap.accel.status == ImuStatus::Saturated && snap.accel.timestamp_ms == 520);
    checkTrue("gyro recovers in the same sample", snap.gyro.valid && snap.gyro.timestamp_ms == 530);
}

// Test group 3: an empty or missing script never crashes and never has data.
static void testEmptyScript() {
    printf("\n-- empty script --\n");
    FakeImuSource fake;
    RawImuSample raw;

    initFakeImuSource(fake, DEFAULT_FAKE_IMU_SCRIPT, 0);
    checkTrue("length 0: no data", !pollFakeImu(fake, raw));
    initFakeImuSource(fake, nullptr, 5);
    checkTrue("no script: no data", !pollFakeImu(fake, raw));
}

int main() {
    testDefaultScript();
    testPipelineMapping();
    testEmptyScript();

    printf("\n%d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}
