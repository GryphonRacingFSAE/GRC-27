// The IMU facts for #44. See ImuDiagnostics.h.

#include <Imu/ImuDiagnostics.h>
#include <Imu/ImuQuality.h>

ImuDiagnostics makeInitialImuDiagnostics() {
    ImuDiagnostics diag;
    diag.initState = ImuInitState::NotStarted;
    diag.initAttempts = 0;
    diag.accelGoodCount = 0;
    diag.accelFailCount = 0;
    diag.gyroGoodCount = 0;
    diag.gyroFailCount = 0;
    diag.latest = makeUnavailableImuSnapshot();
    return diag;
}

void recordInitResult(ImuDiagnostics& diag, ImuSnapshot& snapshot, bool ok) {
    diag.initAttempts++;
    diag.initState = ok ? ImuInitState::Ok : ImuInitState::Failed;
    if (!ok) {
        markFailed(snapshot.accel, ImuStatus::InitFailed);
        markFailed(snapshot.gyro, ImuStatus::InitFailed);
    }
    diag.latest = snapshot;
}

void recordSampleResult(ImuDiagnostics& diag, const ImuSnapshot& snapshot) {
    if (snapshot.accel.valid && snapshot.accel.status == ImuStatus::Ok) {
        diag.accelGoodCount++;
    } else {
        diag.accelFailCount++;
    }

    if (snapshot.gyro.valid && snapshot.gyro.status == ImuStatus::Ok) {
        diag.gyroGoodCount++;
    } else {
        diag.gyroFailCount++;
    }

    diag.latest = snapshot;
}

// A failed read keeps the last good time, so this is the age of the last good
// sample. A good count of 0 means there has never been one.
bool accelSampleAge(const ImuDiagnostics& diag, uint32_t now_ms, uint32_t& age_ms) {
    if (diag.accelGoodCount == 0) {
        return false;
    }
    age_ms = now_ms - diag.latest.accel.timestamp_ms;
    return true;
}

bool gyroSampleAge(const ImuDiagnostics& diag, uint32_t now_ms, uint32_t& age_ms) {
    if (diag.gyroGoodCount == 0) {
        return false;
    }
    age_ms = now_ms - diag.latest.gyro.timestamp_ms;
    return true;
}

const char* imuStatusName(ImuStatus status) {
    switch (status) {
        case ImuStatus::NotStarted:   return "NotStarted";
        case ImuStatus::Ok:           return "Ok";
        case ImuStatus::InitFailed:   return "InitFailed";
        case ImuStatus::ReadFailed:   return "ReadFailed";
        case ImuStatus::Saturated:    return "Saturated";
        case ImuStatus::InvalidValue: return "InvalidValue";
        default:                      return "Unknown";
    }
}

const char* imuInitStateName(ImuInitState state) {
    switch (state) {
        case ImuInitState::NotStarted: return "NotStarted";
        case ImuInitState::Ok:         return "Ok";
        case ImuInitState::Failed:     return "Failed";
        default:                       return "Unknown";
    }
}
