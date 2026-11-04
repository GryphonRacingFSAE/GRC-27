/* The IMU data shared with CAN (#43) and serial (#44).
 *
 * Car axes (right-handed):
 *   longitudinal: + forward    roll rate spins around this axis
 *   lateral:      + left       pitch rate spins around this axis
 *   vertical:     + up         yaw rate spins around this axis
 * Positive rotation: point your right thumb along the + axis, and your fingers
 * curl the positive way.
 *
 * Acceleration includes gravity: a level, parked car reads about +1 g vertical.
 *
 * NOTE: timestamps come from millis(), which wraps after ~49.7 days. Get an age
 * with (now - timestamp_ms). Never compare two timestamps with < or >.
 * NOTE: valid=true does not mean recent. Use isFresh() to check both.
 * NOTE: if #43 needs different axis signs, update this comment, the table in
 * ImuConversion.h and the tests together.
 * TODO(#40): move this into include/Utils/API.h if Riyan agrees.
 */

#ifndef IMU_TYPES_H
#define IMU_TYPES_H

#include <stdint.h>
#include <math.h>

// A reading older than this is stale: too old to use as current.
// NOTE: if #43 needs a different limit, change it here only.
constexpr uint32_t IMU_FRESHNESS_LIMIT_MS = 100;

// Readings per second we ask the sensor for (one every 10 ms).
constexpr uint32_t IMU_SAMPLE_RATE_HZ = 100;

// Why a reading is or isn't usable. Only Ok means valid=true.
enum class ImuStatus : uint8_t {
    NotStarted,     // no reading attempted yet
    Ok,             // read and passed the checks
    InitFailed,     // the sensor could not be started
    ReadFailed,     // the sensor started, but the last read failed
    Saturated,      // hit the sensor's limit, so the real value may be bigger
    InvalidValue    // the read worked but gave NaN or infinity
};
// NOTE: #43 decides whether a Saturated reading is still worth sending.

// One acceleration reading in car directions, in g.
struct ImuAccel {
    float longitudinal_g;   // + = forward
    float lateral_g;        // + = left
    float vertical_g;       // + = up; about +1 g when parked
    bool valid;             // true only if read and checked
    ImuStatus status;       // why it is or isn't valid
    uint32_t timestamp_ms;  // when it was read
};

// One rotation-rate reading in car directions, in degrees per second.
// This is how fast the car is turning, not an angle.
struct ImuGyro {
    float rollRate_dps;     // around the forward axis
    float pitchRate_dps;    // around the left axis
    float yawRate_dps;      // around the up axis
    bool valid;             // true only if read and checked
    ImuStatus status;       // why it is or isn't valid
    uint32_t timestamp_ms;  // when it was read
};

// One full update. Accel and gyro are checked separately, so one can still be
// used if the other fails.
struct ImuSnapshot {
    ImuAccel accel;
    ImuGyro gyro;
};

/* Gives a snapshot that means "no data yet".
 * Values are NaN, not 0, because 0 is a real reading.
 */
inline ImuSnapshot makeUnavailableImuSnapshot() {
    ImuSnapshot snapshot;

    snapshot.accel.longitudinal_g = NAN;
    snapshot.accel.lateral_g = NAN;
    snapshot.accel.vertical_g = NAN;
    snapshot.accel.valid = false;
    snapshot.accel.status = ImuStatus::NotStarted;
    snapshot.accel.timestamp_ms = 0;

    snapshot.gyro.rollRate_dps = NAN;
    snapshot.gyro.pitchRate_dps = NAN;
    snapshot.gyro.yawRate_dps = NAN;
    snapshot.gyro.valid = false;
    snapshot.gyro.status = ImuStatus::NotStarted;
    snapshot.gyro.timestamp_ms = 0;

    return snapshot;
}

#endif // IMU_TYPES_H
