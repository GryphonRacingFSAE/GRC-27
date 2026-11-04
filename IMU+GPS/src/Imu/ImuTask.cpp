/* The FreeRTOS side of the IMU task: sleeping, the lock and debug printing.
 * The decisions are in ImuRunner.cpp, which is tested on the laptop.
 */

#include <Imu/ImuTask.h>
#include <Imu/ImuConversion.h>
#include <Imu/ImuRunner.h>
#include <Imu/ImuTypes.h>

#ifdef IMU_USE_FAKE_SOURCE
#warning "FAKE IMU SOURCE SELECTED - this firmware must not go on the car"
#include <Imu/ImuFakeSource.h>
#else
#include <Imu/ImuIcm20948.h>
#endif

#include <Arduino.h>
#include <math.h>

// TODO(#40): the stack size and priority are estimates, not measured or agreed.
static const uint32_t IMU_TASK_STACK_BYTES = 4096;
static const UBaseType_t IMU_TASK_PRIORITY = 1;

// Sleep between loops. 5 ms picks up a new 100 Hz sample quickly, and the sleep
// lets other tasks run. It must never be 0.
static const uint32_t IMU_POLL_PERIOD_MS = 5;

// The longest anyone waits for the lock. Copying is quick, so this should rarely
// run out. If it does, we give up instead of getting stuck.
static const TickType_t IMU_LOCK_WAIT_TICKS = pdMS_TO_TICKS(2);

// Only touch sharedLatest while holding the lock.
static SemaphoreHandle_t latestLock = nullptr;
static ImuDiagnostics sharedLatest;
static TaskHandle_t imuTaskHandle = nullptr;

// Which sensor this firmware uses.
#ifdef IMU_USE_FAKE_SOURCE
static FakeImuSource fakeSource;

static bool fakeInit() {
    initFakeImuSource(fakeSource, DEFAULT_FAKE_IMU_SCRIPT, DEFAULT_FAKE_IMU_SCRIPT_LENGTH);
    return true;
}

// The script moves every 10 ms but the task loops every 5 ms, so wait for a full
// tick to keep the script's timing right.
static bool fakePoll(RawImuSample& out) {
    static uint32_t lastTick_ms = 0;
    uint32_t now = millis();
    if (now - lastTick_ms < (1000 / IMU_SAMPLE_RATE_HZ)) {
        return false;
    }
    lastTick_ms = now;
    return pollFakeImu(fakeSource, out);
}

static const ImuSource IMU_SOURCE = {fakeInit, fakePoll};
#else
static const ImuSource IMU_SOURCE = {imuIcmInit, imuIcmPoll};
#endif

// Lets ImuRunner.cpp get the time without needing Arduino.h.
static uint32_t clockMs() {
    return millis();
}

// Copies the record into the shared spot. Returns false if the lock was busy.
static bool publishLatest(const ImuDiagnostics& diag) {
    if (xSemaphoreTake(latestLock, IMU_LOCK_WAIT_TICKS) != pdTRUE) {
        return false;
    }
    sharedLatest = diag;
    xSemaphoreGive(latestLock);
    return true;
}

// Runs forever. Sensor work happens with the lock NOT held.
static void imuTask(void* parameters) {
    (void)parameters;

    // "static" keeps this big record off the task's small stack.
    static ImuRunner runner = makeImuRunner();
    bool unpublished = false;

    for (;;) {
        // TODO(#41, mounting): swap in the real table once it is measured.
        if (imuRunnerStep(runner, IMU_SOURCE, PLACEHOLDER_IMU_MOUNTING, clockMs)) {
            unpublished = true;
        }
        if (unpublished && publishLatest(runner.diag)) {
            unpublished = false;
        }
        vTaskDelay(pdMS_TO_TICKS(IMU_POLL_PERIOD_MS));
    }
}

// Prints the IMU values every 200 ms. Built only with -DIMU_DEBUG_PRINT.
// Remove it once #44's serial output exists.
#ifdef IMU_DEBUG_PRINT
static void imuDebugTask(void* parameters) {
    (void)parameters;

    for (;;) {
        ImuDiagnostics diag;
        if (imuGetLatest(diag)) {
            const uint32_t now = millis();
            const ImuAccel& a = diag.latest.accel;
            const ImuGyro& g = diag.latest.gyro;
            uint32_t accelAge = 0;
            uint32_t gyroAge = 0;
            const bool haveAccel = accelSampleAge(diag, now, accelAge);
            const bool haveGyro = gyroSampleAge(diag, now, gyroAge);
            const float total = sqrtf(a.longitudinal_g * a.longitudinal_g + a.lateral_g * a.lateral_g +
                                      a.vertical_g * a.vertical_g);

            Serial.printf("init=%s tries=%lu | accel g lon=%+.3f lat=%+.3f vert=%+.3f total=%.3f %s ",
                          imuInitStateName(diag.initState), (unsigned long)diag.initAttempts,
                          a.longitudinal_g, a.lateral_g, a.vertical_g, total, imuStatusName(a.status));
            if (haveAccel) {
                Serial.printf("age=%lums ", (unsigned long)accelAge);
            } else {
                Serial.print("age=never ");
            }
            Serial.printf("fail=%lu | gyro dps roll=%+.2f pitch=%+.2f yaw=%+.2f %s ",
                          (unsigned long)diag.accelFailCount,
                          g.rollRate_dps, g.pitchRate_dps, g.yawRate_dps, imuStatusName(g.status));
            if (haveGyro) {
                Serial.printf("age=%lums ", (unsigned long)gyroAge);
            } else {
                Serial.print("age=never ");
            }
            Serial.printf("fail=%lu\n", (unsigned long)diag.gyroFailCount);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
#endif

bool imuTaskStart() {
    if (imuTaskHandle != nullptr) {
        return true;
    }

    // Fill the record before anyone can read it.
    sharedLatest = makeInitialImuDiagnostics();

    latestLock = xSemaphoreCreateMutex();
    if (latestLock == nullptr) {
        return false;
    }

    if (xTaskCreate(imuTask, "ImuTask", IMU_TASK_STACK_BYTES, nullptr, IMU_TASK_PRIORITY, &imuTaskHandle) != pdPASS) {
        return false;
    }

#ifdef IMU_DEBUG_PRINT
    // Debug only, so a failure here doesn't fail the IMU.
    xTaskCreate(imuDebugTask, "ImuDebug", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr);
#endif
    return true;
}

bool imuGetLatest(ImuDiagnostics& out) {
    if (latestLock == nullptr) {
        return false;
    }
    if (xSemaphoreTake(latestLock, IMU_LOCK_WAIT_TICKS) != pdTRUE) {
        return false;
    }
    out = sharedLatest;
    xSemaphoreGive(latestLock);
    return true;
}
