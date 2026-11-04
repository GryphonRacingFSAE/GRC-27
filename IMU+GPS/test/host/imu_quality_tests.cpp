/* Laptop tests for ImuQuality: accepting readings, failures and freshness.
 *
 * Build and run in WSL from the IMU+GPS folder:
 *   g++ -std=c++17 -Wall -Wextra -Iinclude test/host/imu_quality_tests.cpp src/Imu/ImuQuality.cpp -o imu_quality_tests
 *   ./imu_quality_tests
 *
 * NOTE: checkFloat and checkTrue are copied from imu_conversion_tests.cpp. See
 * the clean-up note in imu_fake_source_tests.cpp.
 */

#include <Imu/ImuQuality.h>
#include <math.h>
#include <stdio.h>

// Score counters and helpers (same as in imu_conversion_tests.cpp).
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

// Test group 1: isFiniteReading. Real numbers in, NaN/infinity out.
static void testFiniteCheck() {
    printf("\n-- isFiniteReading --\n");
    checkTrue("accepts positive values", isFiniteReading(1.0f, 2.0f, 3.0f));
    checkTrue("accepts zero (a real measurement)", isFiniteReading(0.0f, 0.0f, 0.0f));
    checkTrue("accepts negative values", isFiniteReading(-0.25f, -1.0f, -300.0f));
    checkTrue("rejects NaN on any axis", !isFiniteReading(1.0f, NAN, 3.0f));
    checkTrue("rejects +infinity", !isFiniteReading(INFINITY, 0.0f, 0.0f));
    checkTrue("rejects -infinity", !isFiniteReading(0.0f, 0.0f, -INFINITY));
}

// Test group 1b: isRawSaturated. Only the two ends of int16 count.
static void testSaturationCheck() {
    printf("\n-- isRawSaturated --\n");
    checkTrue("normal counts are not saturated", !isRawSaturated(100, -2000, 4096));
    checkTrue("zero is not saturated", !isRawSaturated(0, 0, 0));
    checkTrue("one below the top (32766) is not saturated", !isRawSaturated(32766, -32767, 0));
    checkTrue("32767 on any axis is saturated", isRawSaturated(0, 32767, 0));
    checkTrue("-32768 on any axis is saturated", isRawSaturated(0, 0, -32768));
}

// Test group 2: isAgeWithinLimit, including the millis() wraparound.
static void testAge() {
    printf("\n-- isAgeWithinLimit --\n");
    checkTrue("age 0 ms is within 100", isAgeWithinLimit(5000, 5000, 100));
    checkTrue("age 100 ms is within 100 (edge counts as fresh)", isAgeWithinLimit(5000, 5100, 100));
    checkTrue("age 101 ms is NOT within 100", !isAgeWithinLimit(5000, 5101, 100));

    // Rollover: reading taken 6 ms before millis() wrapped, now = 5 after.
    // 4294967290 -> 4294967295 is 5 ms, 4294967295 -> 0 is 1 ms, 0 -> 5 is
    // 5 ms: total age 11 ms.
    checkTrue("rollover: age 11 ms across wrap is fresh", isAgeWithinLimit(4294967290u, 5, 100));
    checkTrue("rollover: age 206 ms across wrap is stale", !isAgeWithinLimit(4294967290u, 200, 100));

    // Timestamp 1 ms "in the future" gives a huge age, so it is stale.
    checkTrue("timestamp after now counts as stale", !isAgeWithinLimit(5001, 5000, 100));
}

// Test group 3: the life of one accel reading, step by step:
// start -> good -> zero/negative -> NaN -> read failed -> recovery.
static void testAccelLifecycle() {
    printf("\n-- accel lifecycle --\n");
    ImuSnapshot snap = makeUnavailableImuSnapshot();

    // Starting state: not valid, not fresh at any time.
    checkTrue("start: not fresh", !isFresh(snap.accel, 0));
    checkTrue("start: status NotStarted", snap.accel.status == ImuStatus::NotStarted);

    // Good reading at t=1000. Distinct values per axis so swaps would show.
    checkTrue("good reading accepted", acceptReading(snap.accel, 0.5f, -0.25f, 1.0f, 1000));
    checkTrue("good: valid + status Ok", snap.accel.valid && snap.accel.status == ImuStatus::Ok);
    checkTrue("good: timestamp = acquisition time 1000", snap.accel.timestamp_ms == 1000);
    checkFloat("good: longitudinal stored", snap.accel.longitudinal_g, 0.5f);
    checkFloat("good: lateral stored", snap.accel.lateral_g, -0.25f);
    checkFloat("good: vertical stored", snap.accel.vertical_g, 1.0f);
    checkTrue("good: fresh at 1050", isFresh(snap.accel, 1050));
    checkTrue("good: stale at 1101 (still valid, just old)", !isFresh(snap.accel, 1101) && snap.accel.valid);

    // Zero and negative are real measurements.
    checkTrue("all-zero reading accepted", acceptReading(snap.accel, 0.0f, 0.0f, 0.0f, 1200));
    checkTrue("zero: valid", snap.accel.valid);
    checkTrue("all-negative reading accepted", acceptReading(snap.accel, -1.0f, -2.0f, -3.0f, 1300));
    checkFloat("negative: lateral stored", snap.accel.lateral_g, -2.0f);

    // NaN reading: rejected, old values and OLD time kept.
    checkTrue("NaN reading rejected", !acceptReading(snap.accel, NAN, 0.0f, 0.0f, 1400));
    checkTrue("NaN: not valid, status InvalidValue",
              !snap.accel.valid && snap.accel.status == ImuStatus::InvalidValue);
    checkFloat("NaN: old longitudinal kept", snap.accel.longitudinal_g, -1.0f);
    checkTrue("NaN: old timestamp 1300 kept (not 1400)", snap.accel.timestamp_ms == 1300);
    checkTrue("NaN: not fresh even though recent", !isFresh(snap.accel, 1400));

    // Read failed: same idea.
    markFailed(snap.accel, ImuStatus::ReadFailed);
    checkTrue("read failed: status ReadFailed", snap.accel.status == ImuStatus::ReadFailed);
    checkTrue("read failed: old timestamp 1300 kept", snap.accel.timestamp_ms == 1300);

    // Recovery: next good reading becomes valid again with its own time.
    checkTrue("recovery accepted", acceptReading(snap.accel, 0.1f, 0.2f, 0.9f, 1600));
    checkTrue("recovery: valid, Ok, timestamp 1600",
              snap.accel.valid && snap.accel.status == ImuStatus::Ok && snap.accel.timestamp_ms == 1600);
    checkTrue("recovery: fresh at 1650", isFresh(snap.accel, 1650));
}

// Test group 4: gyro uses the same rules, and fails separately from accel.
static void testGyroSeparate() {
    printf("\n-- gyro (separate from accel) --\n");
    ImuSnapshot snap = makeUnavailableImuSnapshot();

    acceptReading(snap.accel, 0.0f, 0.0f, 1.0f, 2000);
    checkTrue("gyro good reading accepted", acceptReading(snap.gyro, 10.0f, -20.0f, 30.0f, 2000));
    checkFloat("gyro: roll stored", snap.gyro.rollRate_dps, 10.0f);
    checkFloat("gyro: pitch stored", snap.gyro.pitchRate_dps, -20.0f);
    checkFloat("gyro: yaw stored", snap.gyro.yawRate_dps, 30.0f);

    // Gyro fails; accel must be unaffected.
    checkTrue("gyro infinity rejected", !acceptReading(snap.gyro, 0.0f, INFINITY, 0.0f, 2010));
    checkTrue("gyro: not fresh after failure", !isFresh(snap.gyro, 2010));
    checkTrue("accel: still fresh (independent)", isFresh(snap.accel, 2010));
    checkTrue("gyro: old timestamp 2000 kept", snap.gyro.timestamp_ms == 2000);
}

// Test group 5: markFailed never records a failure as Ok/NotStarted.
static void testFailureReasonGuard() {
    printf("\n-- markFailed reason guard --\n");
    ImuSnapshot snap = makeUnavailableImuSnapshot();
    acceptReading(snap.gyro, 1.0f, 2.0f, 3.0f, 100);

    markFailed(snap.gyro, ImuStatus::Ok);
    checkTrue("Ok as failure reason becomes ReadFailed",
              !snap.gyro.valid && snap.gyro.status == ImuStatus::ReadFailed);
    markFailed(snap.gyro, ImuStatus::NotStarted);
    checkTrue("NotStarted as failure reason becomes ReadFailed", snap.gyro.status == ImuStatus::ReadFailed);
    markFailed(snap.gyro, ImuStatus::InitFailed);
    checkTrue("InitFailed is kept as-is", snap.gyro.status == ImuStatus::InitFailed);
}

// main: run all test groups, print the score.
// Exit code 0 = everything passed, 1 = at least one check failed.
int main() {
    testFiniteCheck();
    testSaturationCheck();
    testAge();
    testAccelLifecycle();
    testGyroSeparate();
    testFailureReasonGuard();

    printf("\n%d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}
