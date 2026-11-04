/* Laptop tests for ImuRunner. Fakes the sensor and clock to check what the task
 * does when the IMU is missing, comes back or has no data.
 * NOTE: this doesn't test real FreeRTOS behaviour. That needs a real board.
 *
 * Build and run in WSL from the IMU+GPS folder:
 *   g++ -std=c++17 -Wall -Wextra -Iinclude test/host/imu_runner_tests.cpp src/Imu/ImuRunner.cpp src/Imu/ImuDiagnostics.cpp src/Imu/ImuFakeSource.cpp src/Imu/ImuPipeline.cpp src/Imu/ImuQuality.cpp src/Imu/ImuConversion.cpp -o imu_runner_tests
 *   ./imu_runner_tests
 *
 * NOTE: checkTrue is copied here too. See the clean-up note in
 * imu_fake_source_tests.cpp.
 */

#include <Imu/ImuRunner.h>
#include <Imu/ImuFakeSource.h>
#include <Imu/ImuQuality.h>
#include <stdio.h>

static int passCount = 0;
static int failCount = 0;

static void checkTrue(const char* name, bool condition) {
    printf("%s  %s\n", condition ? "PASS" : "FAIL", name);
    if (condition) {
        passCount++;
    } else {
        failCount++;
    }
}

// The pretend world. Each test sets these up with resetWorld().
static uint32_t fakeNow = 0;        // the pretend millis()
static int initCalls = 0;           // how many times init was called
static int pollCalls = 0;           // how many times poll was called
static int initFailuresLeft = 0;    // init fails this many times, then works
static uint32_t initCost_ms = 0;    // how long a start-up attempt pretends to take
static uint32_t pollCost_ms = 0;    // how long a read pretends to take
static bool sampleWaiting = false;  // is a good sample ready to be read?

static void resetWorld(uint32_t startTime_ms) {
    fakeNow = startTime_ms;
    initCalls = 0;
    pollCalls = 0;
    initFailuresLeft = 0;
    initCost_ms = 0;
    pollCost_ms = 0;
    sampleWaiting = false;
}

static uint32_t fakeClock() {
    return fakeNow;
}

static bool scriptedInit() {
    initCalls++;
    fakeNow += initCost_ms;
    if (initFailuresLeft > 0) {
        initFailuresLeft--;
        return false;
    }
    return true;
}

static bool scriptedPoll(RawImuSample& out) {
    pollCalls++;
    if (!sampleWaiting) {
        return false;
    }
    sampleWaiting = false;
    fakeNow += pollCost_ms;
    out = {true, true, {100.0f, 200.0f, 1000.0f}, {1.5f, 2.5f, 3.5f}, false, false};
    return true;
}

static const ImuSource SCRIPTED_SOURCE = {scriptedInit, scriptedPoll};

// Test group 1: the IMU is missing for the whole run.
static void testMissingImu() {
    printf("\n-- missing IMU: 5 simulated seconds, one step every 5 ms --\n");
    resetWorld(1000);
    initFailuresLeft = 1000000;

    ImuRunner runner = makeImuRunner();
    int changes = 0;
    for (uint32_t step = 0; step < 1000; step++) {  // 1000 steps * 5 ms = 5 s
        if (imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock)) {
            changes++;
        }
        fakeNow += 5;
    }

    checkTrue("5 attempts in 5 s, not 1000 (no tight retry)", initCalls == 5);
    checkTrue("every attempt is recorded", runner.diag.initAttempts == 5);
    checkTrue("only attempts count as changes to share", changes == 5);
    checkTrue("sensor is never read while not started", pollCalls == 0);
    checkTrue("init state is Failed", runner.diag.initState == ImuInitState::Failed);
    checkTrue("accel says InitFailed", runner.diag.latest.accel.status == ImuStatus::InitFailed);
    checkTrue("gyro says InitFailed", runner.diag.latest.gyro.status == ImuStatus::InitFailed);
    checkTrue("no data is marked valid", !runner.diag.latest.accel.valid && !runner.diag.latest.gyro.valid);

    uint32_t age = 0;
    checkTrue("sample age is 'never', not 0 ms", !accelSampleAge(runner.diag, fakeNow, age));
}

// Test group 2: a slow start-up attempt still gets a full pause after it.
static void testSlowInitStillPauses() {
    printf("\n-- slow attempt: pause counts from the END of the attempt --\n");
    resetWorld(1000);
    initFailuresLeft = 100;
    initCost_ms = 200;

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("first attempt ran and ended at 1200", initCalls == 1 && fakeNow == 1200);

    fakeNow = 2199;  // 999 ms after the attempt ended
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("999 ms later: still waiting", initCalls == 1);

    fakeNow = 2200;  // exactly the limit
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("1000 ms later: tries again", initCalls == 2);
}

// Test group 3: the retry pause across the millis() wraparound.
static void testRetryAcrossRollover() {
    printf("\n-- retry pause across wraparound --\n");
    resetWorld(4294966796u);  // 500 ms before millis() wraps to 0
    initFailuresLeft = 100;

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("first attempt ran", initCalls == 1);

    fakeNow = 4294967000u;  // 204 ms later, still before the wrap
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("204 ms later, before the wrap: still waiting", initCalls == 1);

    fakeNow = 499;  // 999 ms later, on the far side of the wrap
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("999 ms across the wrap: still waiting", initCalls == 1);

    fakeNow = 500;  // 1000 ms later
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("1000 ms across the wrap: tries again", initCalls == 2);
}

// Test group 4: the sensor is plugged in late.
static void testLateRecovery() {
    printf("\n-- IMU fails twice, then starts --\n");
    resetWorld(1000);
    initFailuresLeft = 2;

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);  // t=1000 fails
    fakeNow = 2000;
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);  // t=2000 fails
    fakeNow = 3000;
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);  // t=3000 works

    checkTrue("3 attempts recorded", runner.diag.initAttempts == 3);
    checkTrue("init state is Ok", runner.diag.initState == ImuInitState::Ok);

    fakeNow = 3010;
    sampleWaiting = true;
    bool changed = imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    checkTrue("first sample is reported as a change", changed);
    checkTrue("accel recovers: valid and Ok",
              runner.diag.latest.accel.valid && runner.diag.latest.accel.status == ImuStatus::Ok);
    checkTrue("gyro recovers: valid and Ok",
              runner.diag.latest.gyro.valid && runner.diag.latest.gyro.status == ImuStatus::Ok);
    checkTrue("no more start-up attempts once started", initCalls == 3);
}

// Test group 5: the timestamp is when the sample was READ.
static void testTimestampIsAfterRead() {
    printf("\n-- timestamp comes from after the read --\n");
    resetWorld(1000);
    pollCost_ms = 3;

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);

    fakeNow = 2000;  // the task wakes up here...
    sampleWaiting = true;
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);  // ...the read ends at 2003
    checkTrue("accel time is 2003, not the wakeup 2000", runner.diag.latest.accel.timestamp_ms == 2003);
    checkTrue("gyro time is 2003, not the wakeup 2000", runner.diag.latest.gyro.timestamp_ms == 2003);
}

// Test group 6: no new data must change nothing, so old data ages honestly.
static void testNoDataChangesNothing() {
    printf("\n-- no new data: old time kept, data goes stale --\n");
    resetWorld(1000);

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);

    fakeNow = 1010;
    sampleWaiting = true;
    imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock);

    int changes = 0;
    for (int step = 0; step < 100; step++) {
        fakeNow += 5;
        if (imuRunnerStep(runner, SCRIPTED_SOURCE, PLACEHOLDER_IMU_MOUNTING, fakeClock)) {
            changes++;
        }
    }

    checkTrue("no changes while there is no data", changes == 0);
    checkTrue("timestamp still 1010", runner.diag.latest.accel.timestamp_ms == 1010);
    checkTrue("still 1 good read, 0 failed",
              runner.diag.accelGoodCount == 1 && runner.diag.accelFailCount == 0);
    checkTrue("stale after 500 ms", !isFresh(runner.diag.latest.accel, fakeNow));
}

// Test group 7: a typo in the mounting table stops the sensor being used.
static void testBadMountingTable() {
    printf("\n-- bad mounting table is refused --\n");
    resetWorld(1000);

    // Two rows use sensor X, so sensor Y's data would be lost.
    const AxisMapping badTable = {{SensorAxis::X, +1}, {SensorAxis::X, +1}, {SensorAxis::Z, +1}};

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, SCRIPTED_SOURCE, badTable, fakeClock);

    checkTrue("the sensor is not even started", initCalls == 0);
    checkTrue("reported as a failed start-up", runner.diag.initState == ImuInitState::Failed);
    checkTrue("accel says InitFailed", runner.diag.latest.accel.status == ImuStatus::InitFailed);
    checkTrue("still paced at the retry limit", !imuRunnerStep(runner, SCRIPTED_SOURCE, badTable, fakeClock));
}

// Test group 8: the whole fake script through the same path as the sensor.
static FakeImuSource scriptSource;

static bool scriptInit() {
    initFakeImuSource(scriptSource, DEFAULT_FAKE_IMU_SCRIPT, DEFAULT_FAKE_IMU_SCRIPT_LENGTH);
    return true;
}

static bool scriptPoll(RawImuSample& out) {
    return pollFakeImu(scriptSource, out);
}

static void testFakeScriptAcrossRollover() {
    printf("\n-- fake script, with millis() wrapping part-way through --\n");
    // Starts 50 ms before the wrap, so the 19 ticks run across it.
    resetWorld(4294967246u);
    const ImuSource source = {scriptInit, scriptPoll};

    ImuRunner runner = makeImuRunner();
    imuRunnerStep(runner, source, PLACEHOLDER_IMU_MOUNTING, fakeClock);

    for (uint32_t tick = 0; tick < DEFAULT_FAKE_IMU_SCRIPT_LENGTH; tick++) {
        fakeNow += 10;
        imuRunnerStep(runner, source, PLACEHOLDER_IMU_MOUNTING, fakeClock);
    }

    // Good accel: lines 0-3, 17, 18. Failed: line 16.
    checkTrue("accel: 6 good, 1 failed", runner.diag.accelGoodCount == 6 && runner.diag.accelFailCount == 1);
    // Good gyro: lines 0-3, 18. Failed: lines 16, 17.
    checkTrue("gyro: 5 good, 2 failed", runner.diag.gyroGoodCount == 5 && runner.diag.gyroFailCount == 2);
    // Line 18 is tick 19: 4294967246 + 190 wraps round to 140.
    checkTrue("final timestamp is the wrapped time 140", runner.diag.latest.accel.timestamp_ms == 140);
    checkTrue("final reading is valid", runner.diag.latest.accel.valid && runner.diag.latest.gyro.valid);
    checkTrue("final reading is fresh after the wrap", isFresh(runner.diag.latest.accel, 150));
}

int main() {
    testMissingImu();
    testSlowInitStillPauses();
    testRetryAcrossRollover();
    testLateRecovery();
    testTimestampIsAfterRead();
    testNoDataChangesNothing();
    testBadMountingTable();
    testFakeScriptAcrossRollover();

    printf("\n%d passed, %d failed\n", passCount, failCount);
    return failCount == 0 ? 0 : 1;
}
