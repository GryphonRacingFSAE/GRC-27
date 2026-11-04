/* What the IMU task does on each loop. It is kept apart from FreeRTOS so tests
 * can fake a missing sensor on a laptop.
 *
 * NOTE: start-up isn't retried after it has worked. If the sensor stops answering
 * mid-run, the data goes stale until the board reboots. Revisit this if the
 * sensor drops out on the car.
 */

#ifndef IMU_RUNNER_H
#define IMU_RUNNER_H

#include <Imu/ImuConversion.h>
#include <Imu/ImuDiagnostics.h>
#include <Imu/ImuPipeline.h>
#include <Imu/ImuTypes.h>
#include <stdint.h>

// Minimum wait between start-up attempts, so a missing IMU can't cause a tight
// retry loop. Counted from the END of the last attempt.
constexpr uint32_t IMU_INIT_RETRY_MS = 1000;

// Which sensor to use. Holding the functions here lets the real driver, the fake
// and the tests all plug into the same code.
struct ImuSource {
    bool (*init)();                  // one start-up attempt; true if it worked
    bool (*poll)(RawImuSample& out); // true if a read was attempted
};

// What the task remembers between loops.
struct ImuRunner {
    ImuSnapshot snapshot;         // working copy of the latest data
    ImuDiagnostics diag;          // the record shared with #43 and #44
    bool sensorReady;             // has start-up worked?
    bool initTried;               // has start-up been tried at all?
    uint32_t lastInitAttempt_ms;  // when the last attempt finished
};

// Gives a runner that hasn't done anything yet.
ImuRunner makeImuRunner();

/* Does one loop's work and returns straight away:
 *   sensor not started: try to start it, at most once per IMU_INIT_RETRY_MS
 *   sensor started:     read it, and record the result if a read was attempted
 * Returns true if runner.diag changed and should be shared.
 * clock gives the time in ms (millis() on the board, a fake clock in tests).
 * A bad mounting table counts as a failed start-up.
 */
bool imuRunnerStep(ImuRunner& runner, const ImuSource& source, const AxisMapping& mapping, uint32_t (*clock)());

#endif // IMU_RUNNER_H
