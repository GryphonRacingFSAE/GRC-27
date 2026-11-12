#include "TorquePower.h"

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kWattsPerMechanicalHp = 745.699872; // NIST SP 811 B.9

double torqueNm(double force_n, double arm_m) {
    return force_n * arm_m;
}

double radPerSFromRpm(double rpm) {
    return rpm * 2.0 * kPi / 60.0;
}

double powerW(double torque_nm, double rad_per_s) {
    return torque_nm * rad_per_s;
}

double kilowattsFromWatts(double watts) {
    return watts / 1000.0;
}

double mechanicalHpFromWatts(double watts) {
    return watts / kWattsPerMechanicalHp;
}

double metresFromMillimetres(double millimetres) {
    return millimetres / 1000.0;
}

const char* calcReasonName(CalcReason reason) {
    switch (reason) {
    case CalcReason::None: return "None";
    case CalcReason::ForceUnavailable: return "ForceUnavailable";
    case CalcReason::CalibrationNotReady: return "CalibrationNotReady";
    case CalcReason::ForceNotFinite: return "ForceNotFinite";
    case CalcReason::ForceStale: return "ForceStale";
    case CalcReason::GeometryUnconfirmed: return "GeometryUnconfirmed";
    case CalcReason::ArmInvalid: return "ArmInvalid";
    case CalcReason::ShaftSpeedUnconfirmed: return "ShaftSpeedUnconfirmed";
    case CalcReason::RpmMissing: return "RpmMissing";
    case CalcReason::RpmInvalid: return "RpmInvalid";
    case CalcReason::RpmStale: return "RpmStale";
    case CalcReason::SourceSkewTooLarge: return "SourceSkewTooLarge";
    case CalcReason::Count: break;
    }
    return "?";
}

ShaftSpeedSample engineSpeedFromEcu(const EcuState& ecu) {
    const EcuMessageStatus& engine = ecu.message(EcuMessage::Engine1);
    ShaftSpeedSample s;
    s.received = engine.received;
    s.rpm = ecu.value(EcuSignal::EngineSpeed);
    s.timestamp_ms = engine.last_rx_ms;
    return s;
}

// Age < limit. Unsigned subtraction keeps the age right when millis() wraps;
// a timestamp newer than now gives a huge age, so it reads as stale.
static bool isFresh(uint32_t timestamp_ms, uint32_t now_ms, uint32_t limit_ms) {
    return now_ms - timestamp_ms < limit_ms;
}

// How far apart two timestamps are, either way round, across a millis() wrap.
static uint32_t gapMs(uint32_t a_ms, uint32_t b_ms) {
    const uint32_t a_minus_b = a_ms - b_ms;
    const uint32_t b_minus_a = b_ms - a_ms;
    return a_minus_b < b_minus_a ? a_minus_b : b_minus_a;
}

// The first reason the force itself can't be used, or None.
static CalcReason forceProblem(const ForceMeasurement& force, const CalcLimits& limits, uint32_t now_ms) {
    if (!force.available) {
        return CalcReason::ForceUnavailable;
    }
    if (!force.calibration_ready) {
        return CalcReason::CalibrationNotReady;
    }
    if (!isfinite(force.force_n)) {
        return CalcReason::ForceNotFinite;
    }
    if (!isFresh(force.timestamp_ms, now_ms, limits.force_freshness_ms)) {
        return CalcReason::ForceStale;
    }
    return CalcReason::None;
}

// The first reason the geometry can't be used, or None.
static CalcReason geometryProblem(const MechanicalSetup& setup, const CalcLimits& limits) {
    if (!setup.geometry_confirmed) {
        return CalcReason::GeometryUnconfirmed;
    }
    // Written so NaN fails too (every comparison with NaN is false).
    const double arm = setup.effective_arm_m;
    if (!(isfinite(arm) && arm > 0.0 && arm <= limits.max_arm_m)) {
        return CalcReason::ArmInvalid;
    }
    return CalcReason::None;
}

// The first reason power can't be trusted (given valid torque), or None.
static CalcReason powerProblem(const ForceMeasurement& force, const ShaftSpeedSample& speed,
                               const MechanicalSetup& setup, const CalcLimits& limits, uint32_t now_ms) {
    if (!setup.same_shaft_confirmed) {
        return CalcReason::ShaftSpeedUnconfirmed;
    }
    if (!speed.received) {
        return CalcReason::RpmMissing;
    }
    if (!(isfinite(speed.rpm) && speed.rpm >= 0.0)) {
        return CalcReason::RpmInvalid;
    }
    if (!isFresh(speed.timestamp_ms, now_ms, limits.rpm_freshness_ms)) {
        return CalcReason::RpmStale;
    }
    if (gapMs(force.timestamp_ms, speed.timestamp_ms) > limits.max_source_skew_ms) {
        return CalcReason::SourceSkewTooLarge;
    }
    return CalcReason::None;
}

TorquePowerResult computeTorquePower(const ForceMeasurement& force, const ShaftSpeedSample& speed,
                                     const MechanicalSetup& setup, const CalcLimits& limits, uint32_t now_ms) {
    TorquePowerResult r; // starts invalid, every value NaN
    r.force_timestamp_ms = force.timestamp_ms;
    r.rpm_timestamp_ms = speed.timestamp_ms;

    const CalcReason force_problem = forceProblem(force, limits, now_ms);
    if (force_problem == CalcReason::None) {
        // Only ever the force's own pair: a result is never relabelled with
        // a newer calibration than the force it came from.
        r.versions_known = true;
        r.calibration_version = force.calibration_version;
        r.tare_version = force.tare_version;
    }

    const CalcReason torque_problem =
        force_problem != CalcReason::None ? force_problem : geometryProblem(setup, limits);
    if (torque_problem != CalcReason::None) {
        r.torque.reason = torque_problem;
        r.power.reason = torque_problem;
        return r;
    }
    r.torque.valid = true;
    r.torque.torque_nm = torqueNm(force.force_n, setup.effective_arm_m);
    r.reverse_torque = r.torque.torque_nm < 0.0;

    const CalcReason power_problem = powerProblem(force, speed, setup, limits, now_ms);
    if (power_problem != CalcReason::None) {
        r.power.reason = power_problem;
        return r;
    }

    const double watts = powerW(r.torque.torque_nm, radPerSFromRpm(speed.rpm));
    r.power.valid = true;
    r.power.power_w = watts;
    r.power.power_kw = kilowattsFromWatts(watts);
    r.power.horsepower_hp = mechanicalHpFromWatts(watts);
    return r;
}
