// ============================================================================
// Example dataset for #38 (CSV): torque/power inputs with their expected
// results, valid and invalid. test_torque_power checks every row, so #38 can
// trust it as a fixture for its own formatter/aggregation tests. The rows are
// also #37's issue-table tests: don't remove or change one without adding
// that case to test_torque_power.
//
// FIXTURE ONLY. The geometry here is issue #37's example (perpendicular
// 0.25 m arm, ECU RPM on the same shaft). It is NOT the real dyno; the real
// setup lives in DynoConfig.h and stays unconfirmed until the mechanical team
// measures it.
//
// Expected numbers come from issue #37 or were worked out by hand:
//   600 rpm  = 62.831853 rad/s;  25 N.m x that = 1570.796327 W = 2.106473 hp
//   6000 rpm = 628.318531 rad/s; 25 N.m x that = 15707.963268 W = 21.064726 hp
// ============================================================================
#pragma once

#include <math.h>
#include <stddef.h>

#include "TorquePower.h"

// The agreed limits (same values as DynoConfig.h).
inline const CalcLimits kExampleLimits = {500, 2000, 250, 2.0};

struct TorquePowerExample {
    const char* name;
    // Inputs
    ForceMeasurement force; // {available, force_n, timestamp_ms, calibration_ready, calibration_version, tare_version}
    ShaftSpeedSample speed; // {received, rpm, timestamp_ms}
    MechanicalSetup setup;  // {effective_arm_m, geometry_confirmed, same_shaft_confirmed}
    uint32_t now_ms;
    // Expected (NAN = invalid, the CSV cell is blank)
    bool torque_valid;
    CalcReason torque_reason;
    double torque_nm;
    bool power_valid;
    CalcReason power_reason;
    double power_w;
    double power_kw;
    double horsepower_hp;
    bool reverse_torque;
    bool versions_known; // false = the version cells are blank
    uint32_t calibration_version;
    uint32_t tare_version;
};

// clang-format off
inline const TorquePowerExample kTorquePowerExamples[] = {
    // --- valid --------------------------------------------------------------
    {"reference: 100 N, 0.25 m, 600 rpm",
     {true, 100.0, 1000, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 1000,
     true, CalcReason::None, 25.0,
     true, CalcReason::None, 1570.796327, 1.570796, 2.106473,
     false, true, 2, 3},
    {"valid zero force",
     {true, 0.0, 1000, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 1000,
     true, CalcReason::None, 0.0,
     true, CalcReason::None, 0.0, 0.0, 0.0,
     false, true, 2, 3},
    {"valid zero rpm: torque, zero power",
     {true, 100.0, 1000, true, 2, 3}, {true, 0.0, 1000}, {0.25, true, true}, 1000,
     true, CalcReason::None, 25.0,
     true, CalcReason::None, 0.0, 0.0, 0.0,
     false, true, 2, 3},
    {"negative force: signed, flagged reverse",
     {true, -100.0, 1000, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 1000,
     true, CalcReason::None, -25.0,
     true, CalcReason::None, -1570.796327, -1.570796, -2.106473,
     true, true, 2, 3},
    {"6000 rpm",
     {true, 100.0, 1000, true, 2, 3}, {true, 6000.0, 1000}, {0.25, true, true}, 1000,
     true, CalcReason::None, 25.0,
     true, CalcReason::None, 15707.963268, 15.707963, 21.064726,
     false, true, 2, 3},
    // --- torque valid, power invalid ----------------------------------------
    {"rpm never received",
     {true, 100.0, 1000, true, 2, 3}, {false, 0.0, 0}, {0.25, true, true}, 1000,
     true, CalcReason::None, 25.0,
     false, CalcReason::RpmMissing, NAN, NAN, NAN,
     false, true, 2, 3},
    {"rpm stale (2000 ms old)",
     {true, 100.0, 3000, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 3000,
     true, CalcReason::None, 25.0,
     false, CalcReason::RpmStale, NAN, NAN, NAN,
     false, true, 2, 3},
    {"force and rpm 251 ms apart",
     {true, 100.0, 1251, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 1251,
     true, CalcReason::None, 25.0,
     false, CalcReason::SourceSkewTooLarge, NAN, NAN, NAN,
     false, true, 2, 3},
    {"same shaft unconfirmed",
     {true, 100.0, 1000, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, false}, 1000,
     true, CalcReason::None, 25.0,
     false, CalcReason::ShaftSpeedUnconfirmed, NAN, NAN, NAN,
     false, true, 2, 3},
    // --- both invalid -------------------------------------------------------
    {"force stale (500 ms old)",
     {true, 100.0, 1000, true, 2, 3}, {true, 600.0, 1500}, {0.25, true, true}, 1500,
     false, CalcReason::ForceStale, NAN,
     false, CalcReason::ForceStale, NAN, NAN, NAN,
     false, false, 0, 0},
    {"calibration/tare collecting",
     {true, 100.0, 1000, false, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 1000,
     false, CalcReason::CalibrationNotReady, NAN,
     false, CalcReason::CalibrationNotReady, NAN, NAN, NAN,
     false, false, 0, 0},
    {"force unavailable (load-cell fault or no calibration)",
     {false, NAN, 0, false, 0, 0}, {true, 600.0, 1000}, {0.25, true, true}, 1000,
     false, CalcReason::ForceUnavailable, NAN,
     false, CalcReason::ForceUnavailable, NAN, NAN, NAN,
     false, false, 0, 0},
    {"force NaN",
     {true, NAN, 1000, true, 2, 3}, {true, 600.0, 1000}, {0.25, true, true}, 1000,
     false, CalcReason::ForceNotFinite, NAN,
     false, CalcReason::ForceNotFinite, NAN, NAN, NAN,
     false, false, 0, 0},
    {"geometry unconfirmed (today's real config)",
     {true, 100.0, 1000, true, 2, 3}, {true, 600.0, 1000}, {0.0, false, false}, 1000,
     false, CalcReason::GeometryUnconfirmed, NAN,
     false, CalcReason::GeometryUnconfirmed, NAN, NAN, NAN,
     false, true, 2, 3},
    {"arm 250 m (millimetres typed as metres)",
     {true, 100.0, 1000, true, 2, 3}, {true, 600.0, 1000}, {250.0, true, true}, 1000,
     false, CalcReason::ArmInvalid, NAN,
     false, CalcReason::ArmInvalid, NAN, NAN, NAN,
     false, true, 2, 3},
};
// clang-format on

inline constexpr size_t kTorquePowerExampleCount = sizeof(kTorquePowerExamples) / sizeof(kTorquePowerExamples[0]);
