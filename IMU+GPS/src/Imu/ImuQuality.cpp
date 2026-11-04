// Reading checks and freshness code. See ImuQuality.h.

#include <Imu/ImuQuality.h>

#include <math.h>

bool isFiniteReading(float a, float b, float c) {
    return isfinite(a) && isfinite(b) && isfinite(c);
}

// A 16-bit sensor count can't go past these limits.
static bool isAtRawLimit(int16_t value) {
    return value == INT16_MAX || value == INT16_MIN;
}

bool isRawSaturated(int16_t a, int16_t b, int16_t c) {
    return isAtRawLimit(a) || isAtRawLimit(b) || isAtRawLimit(c);
}

// uint32_t numbers wrap like a car odometer, so this subtraction still gives the
// right age after millis() wraps. Example: 5 - 4294967290 gives 11.
bool isAgeWithinLimit(uint32_t timestamp_ms, uint32_t now_ms, uint32_t limit_ms) {
    uint32_t age_ms = now_ms - timestamp_ms;
    return age_ms <= limit_ms;
}

bool isFresh(const ImuAccel& reading, uint32_t now_ms) {
    return reading.valid && isAgeWithinLimit(reading.timestamp_ms, now_ms, IMU_FRESHNESS_LIMIT_MS);
}

bool isFresh(const ImuGyro& reading, uint32_t now_ms) {
    return reading.valid && isAgeWithinLimit(reading.timestamp_ms, now_ms, IMU_FRESHNESS_LIMIT_MS);
}

// Makes sure a failure always has a real reason.
static ImuStatus failureStatus(ImuStatus reason) {
    if (reason == ImuStatus::Ok || reason == ImuStatus::NotStarted) {
        return ImuStatus::ReadFailed;
    }
    return reason;
}

// The values and timestamp are left alone on purpose.
void markFailed(ImuAccel& reading, ImuStatus reason) {
    reading.valid = false;
    reading.status = failureStatus(reason);
}

void markFailed(ImuGyro& reading, ImuStatus reason) {
    reading.valid = false;
    reading.status = failureStatus(reason);
}

// Checks before storing, so a NaN can't overwrite a good old value.
bool acceptReading(ImuAccel& reading, float longitudinal_g, float lateral_g, float vertical_g, uint32_t now_ms) {
    if (!isFiniteReading(longitudinal_g, lateral_g, vertical_g)) {
        markFailed(reading, ImuStatus::InvalidValue);
        return false;
    }
    reading.longitudinal_g = longitudinal_g;
    reading.lateral_g = lateral_g;
    reading.vertical_g = vertical_g;
    reading.valid = true;
    reading.status = ImuStatus::Ok;
    reading.timestamp_ms = now_ms;
    return true;
}

bool acceptReading(ImuGyro& reading, float rollRate_dps, float pitchRate_dps, float yawRate_dps, uint32_t now_ms) {
    if (!isFiniteReading(rollRate_dps, pitchRate_dps, yawRate_dps)) {
        markFailed(reading, ImuStatus::InvalidValue);
        return false;
    }
    reading.rollRate_dps = rollRate_dps;
    reading.pitchRate_dps = pitchRate_dps;
    reading.yawRate_dps = yawRate_dps;
    reading.valid = true;
    reading.status = ImuStatus::Ok;
    reading.timestamp_ms = now_ms;
    return true;
}
