// What the IMU task does on each loop. See ImuRunner.h.

#include <Imu/ImuRunner.h>

ImuRunner makeImuRunner() {
    ImuRunner runner;
    runner.snapshot = makeUnavailableImuSnapshot();
    runner.diag = makeInitialImuDiagnostics();
    runner.sensorReady = false;
    runner.initTried = false;
    runner.lastInitAttempt_ms = 0;
    return runner;
}

bool imuRunnerStep(ImuRunner& runner, const ImuSource& source, const AxisMapping& mapping, uint32_t (*clock)()) {
    if (!runner.sensorReady) {
        // Too soon to try again?
        if (runner.initTried && (clock() - runner.lastInitAttempt_ms) < IMU_INIT_RETRY_MS) {
            return false;
        }

        // A bad table stops here, so init() is never called.
        bool ok = isValidAxisMapping(mapping) && source.init();

        // Read the clock after the attempt, so a slow attempt still gets a full
        // pause before the next one.
        runner.lastInitAttempt_ms = clock();
        runner.initTried = true;
        runner.sensorReady = ok;
        recordInitResult(runner.diag, runner.snapshot, ok);
        return true;
    }

    RawImuSample raw;
    if (!source.poll(raw)) {
        return false;  // nothing new, so the data just gets older
    }

    uint32_t acquired_ms = clock();  // right after the read
    applyRawSample(runner.snapshot, raw, mapping, acquired_ms);
    recordSampleResult(runner.diag, runner.snapshot);
    return true;
}
