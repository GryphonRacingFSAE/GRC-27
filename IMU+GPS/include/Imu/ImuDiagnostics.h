/* The IMU facts for #44 to print: start-up state, read counts, sample age, units
 * and the latest values. They are all in one record, so #44 gets one consistent
 * copy.
 *
 * NOTE: counters wrap after ~497 days at 100 Hz, which is fine for a race car.
 */

#ifndef IMU_DIAGNOSTICS_H
#define IMU_DIAGNOSTICS_H

#include <Imu/ImuTypes.h>
#include <stdint.h>

// Units and meaning as text, so #44 doesn't have to retype them.
constexpr const char* IMU_ACCEL_UNITS = "g";
constexpr const char* IMU_GYRO_UNITS = "deg/s";
constexpr const char* IMU_MEASUREMENT_NOTE =
    "accel includes gravity (level + still = about +1 g up); "
    "gyro is rotation rate, not angle; no orientation fusion";

// Did the sensor start?
enum class ImuInitState : uint8_t {
    NotStarted,  // not tried yet
    Ok,          // the last attempt worked
    Failed       // the last attempt failed
};

// Everything #44 needs, in one record.
struct ImuDiagnostics {
    ImuInitState initState;
    uint32_t initAttempts;     // start-up attempts so far

    uint32_t accelGoodCount;   // accel reads accepted
    uint32_t accelFailCount;   // accel reads that failed or were invalid
    uint32_t gyroGoodCount;    // gyro reads accepted
    uint32_t gyroFailCount;    // gyro reads that failed or were invalid

    ImuSnapshot latest;        // the latest data
};

// Gives a record with no attempts, no counts and no data.
ImuDiagnostics makeInitialImuDiagnostics();

// Records one start-up attempt. On failure, both accel and gyro are marked
// InitFailed so CAN and serial can see why there is no data.
void recordInitResult(ImuDiagnostics& diag, ImuSnapshot& snapshot, bool ok);

// Records one read attempt. Call it once after each applyRawSample().
// NOTE: calling it twice counts the same read twice.
void recordSampleResult(ImuDiagnostics& diag, const ImuSnapshot& snapshot);

// Gives the ms since the last good sample. Returns false if there has never
// been one, so "never" isn't confused with "0 ms ago".
bool accelSampleAge(const ImuDiagnostics& diag, uint32_t now_ms, uint32_t& age_ms);
bool gyroSampleAge(const ImuDiagnostics& diag, uint32_t now_ms, uint32_t& age_ms);

// Short names for printing, such as "Ok" or "ReadFailed".
const char* imuStatusName(ImuStatus status);
const char* imuInitStateName(ImuInitState state);

#endif // IMU_DIAGNOSTICS_H
