/* Laptop tests for ImuDiagnostics: the facts #44 will print.
 *
 * Build and run in WSL from the IMU+GPS folder:
 *   g++ -std=c++17 -Wall -Wextra -Iinclude test/host/imu_diagnostics_tests.cpp src/Imu/ImuDiagnostics.cpp src/Imu/ImuFakeSource.cpp src/Imu/ImuPipeline.cpp src/Imu/ImuQuality.cpp src/Imu/ImuConversion.cpp -o imu_diagnostics_tests
 *   ./imu_diagnostics_tests
 *
 * NOTE: checkFloat and checkTrue are copied here too. See the clean-up note in
 * imu_fake_source_tests.cpp.
 */

#include <Imu/ImuDiagnostics.h>
#include <Imu/ImuFakeSource.h>
#include <Imu/ImuPipeline.h>
#include <math.h>
#include <stdio.h>
#include <string.h>  // strcmp: compares two pieces of text (0 = same)

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

// One IMU task period, in the documented call order.
static void runTick(FakeImuSource& fake, ImuSnapshot& snap, ImuDiagnostics& diag, uint32_t now_ms) {
    RawImuSample raw;
    if (pollFakeImu(fake, raw)) {
        applyRawSample(snap, raw, PLACEHOLDER_IMU_MOUNTING, now_ms);
        recordSampleResult(diag, snap);
    }
}

// Test group 1: starting state - nothing has happened yet.
static void testInitialState() {
    printf("\n-- initial state --\n");
    ImuDiagnostics diag = makeInitialImuDiagnostics();
    uint32_t age = 12345;

    checkTrue("init state NotStarted, 0 attempts", diag.initState == ImuInitState::NotStarted && diag.initAttempts == 0);
    checkTrue("all counts 0", diag.accelGoodCount == 0 && diag.accelFailCount == 0 &&
                              diag.gyroGoodCount == 0 && diag.gyroFailCount == 0);
    checkTrue("accel age: never (returns false)", !accelSampleAge(diag, 5000, age));
    checkTrue("gyro age: never (returns false)", !gyroSampleAge(diag, 5000, age));
    checkTrue("age_ms left untouched when never", age == 12345);
    checkTrue("latest snapshot is 'no data yet'", !diag.latest.accel.valid && isnan(diag.latest.accel.vertical_g));
}

// Test group 2: start-up fails twice, then works.
static void testInitRetries() {
    printf("\n-- start-up attempts --\n");
    ImuDiagnostics diag = makeInitialImuDiagnostics();
    ImuSnapshot snap = makeUnavailableImuSnapshot();

    recordInitResult(diag, snap, false);
    checkTrue("fail 1: state Failed, 1 attempt", diag.initState == ImuInitState::Failed && diag.initAttempts == 1);
    checkTrue("fail 1: snapshot accel + gyro InitFailed",
              snap.accel.status == ImuStatus::InitFailed && snap.gyro.status == ImuStatus::InitFailed);
    checkTrue("fail 1: diag copy also InitFailed", diag.latest.gyro.status == ImuStatus::InitFailed);

    recordInitResult(diag, snap, false);
    recordInitResult(diag, snap, true);
    checkTrue("then success: state Ok, 3 attempts", diag.initState == ImuInitState::Ok && diag.initAttempts == 3);
    checkTrue("success does not fake a reading (still not valid)", !snap.accel.valid);
}

// Test group 3: run the whole fake script and check counts and ages.
// Script: 4 good, 12 no-data, both failed, accel ok + gyro failed, good.
static void testCountsAndAges() {
    printf("\n-- counts and ages over the fake script --\n");
    FakeImuSource fake;
    initFakeImuSource(fake, DEFAULT_FAKE_IMU_SCRIPT, DEFAULT_FAKE_IMU_SCRIPT_LENGTH);
    ImuSnapshot snap = makeUnavailableImuSnapshot();
    ImuDiagnostics diag = makeInitialImuDiagnostics();
    recordInitResult(diag, snap, true);
    uint32_t age = 0;

    // Ticks 0-3 (t=1000..1030): four good reads.
    for (uint32_t t = 1000; t <= 1030; t += 10) {
        runTick(fake, snap, diag, t);
    }
    checkTrue("after 4 good: accel 4 good / 0 failed", diag.accelGoodCount == 4 && diag.accelFailCount == 0);
    checkTrue("accel age at 1030 = 0 ms", accelSampleAge(diag, 1030, age) && age == 0);

    // Ticks 4-15 (t=1040..1150): no data - counts must not change.
    for (uint32_t t = 1040; t <= 1150; t += 10) {
        runTick(fake, snap, diag, t);
    }
    checkTrue("no-data gap does not change counts", diag.accelGoodCount == 4 && diag.accelFailCount == 0);
    checkTrue("accel age at 1150 = 120 ms", accelSampleAge(diag, 1150, age) && age == 120);

    // Tick 16 (t=1160): both failed.
    runTick(fake, snap, diag, 1160);
    checkTrue("failed read: accel + gyro fail count 1", diag.accelFailCount == 1 && diag.gyroFailCount == 1);
    checkTrue("age still from last GOOD sample: 130 ms", accelSampleAge(diag, 1160, age) && age == 130);
    checkTrue("latest copy shows ReadFailed", diag.latest.accel.status == ImuStatus::ReadFailed);

    // Tick 17 (t=1170): accel good, gyro failed.
    runTick(fake, snap, diag, 1170);
    checkTrue("partial: accel 5 good, gyro 2 failed", diag.accelGoodCount == 5 && diag.gyroFailCount == 2);
    checkTrue("partial: accel age 0, gyro age 140",
              accelSampleAge(diag, 1170, age) && age == 0 && gyroSampleAge(diag, 1170, age) && age == 140);

    // Tick 18 (t=1180): recovery.
    runTick(fake, snap, diag, 1180);
    checkTrue("recovery: gyro 5 good, age 0", diag.gyroGoodCount == 5 && gyroSampleAge(diag, 1180, age) && age == 0);
    checkFloat("latest copy holds latest yaw 0.25", diag.latest.gyro.yawRate_dps, 0.25f);
}

// Test group 4: age across the millis() wraparound.
static void testAgeRollover() {
    printf("\n-- age across wraparound --\n");
    ImuDiagnostics diag = makeInitialImuDiagnostics();
    ImuSnapshot snap = makeUnavailableImuSnapshot();
    RawImuSample raw = {true, true, {0.0f, 0.0f, 1000.0f}, {0.0f, 0.0f, 0.0f}, false, false};
    uint32_t age = 0;

    applyRawSample(snap, raw, PLACEHOLDER_IMU_MOUNTING, 4294967290u);
    recordSampleResult(diag, snap);
    checkTrue("sample 6 ms before wrap, now = 20: age 26 ms", accelSampleAge(diag, 20, age) && age == 26);
}

// Test group 5: text for #44.
static void testText() {
    printf("\n-- units and names --\n");
    checkTrue("accel units 'g'", strcmp(IMU_ACCEL_UNITS, "g") == 0);
    checkTrue("gyro units 'deg/s'", strcmp(IMU_GYRO_UNITS, "deg/s") == 0);
    checkTrue("meaning note mentions gravity", strstr(IMU_MEASUREMENT_NOTE, "gravity") != nullptr);
    checkTrue("status name ReadFailed", strcmp(imuStatusName(ImuStatus::ReadFailed), "ReadFailed") == 0);
    checkTrue("status name InvalidValue", strcmp(imuStatusName(ImuStatus::InvalidValue), "InvalidValue") == 0);
    checkTrue("init name Failed", strcmp(imuInitStateName(ImuInitState::Failed), "Failed") == 0);
}

int main() {
    testInitialState();
    testInitRetries();
    testCountsAndAges();
    testAgeRollover();
    testText();

    printf("\n%d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}
