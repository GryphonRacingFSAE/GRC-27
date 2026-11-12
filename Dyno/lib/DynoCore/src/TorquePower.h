// Torque and power from calibrated force and shaft speed (#37).
// Pure: no hardware, no clock, no RTOS. See docs/torque-power.md.
//
//   torque_Nm             = force_N x effective_arm_m
//   shaft_speed_rad_per_s = shaft_rpm x 2 x pi / 60
//   power_W               = torque_Nm x shaft_speed_rad_per_s
//   power_kW              = power_W / 1000
//   mechanical_hp         = power_W / 745.699872   (mechanical hp, NOT metric hp/PS)
//
// Never use the "torque x rpm / 5252" shortcut: it expects pound-feet.
#pragma once

#include <math.h>
#include <stdint.h>

#include "EcuState.h"
#include "ForceMeasurement.h"

// ---------------------------------------------------------------------------
// Maths. Units are in every name; nothing here checks validity.

double torqueNm(double force_n, double arm_m);
double radPerSFromRpm(double rpm);
double powerW(double torque_nm, double rad_per_s);
double kilowattsFromWatts(double watts);
double mechanicalHpFromWatts(double watts);
double metresFromMillimetres(double millimetres);

// ---------------------------------------------------------------------------
// Inputs

// The shaft speed used for power, with when it was measured.
struct ShaftSpeedSample {
    bool received = false;     // false = never received; rpm must not be used
    double rpm = 0.0;          // revolutions per minute of the shaft
    uint32_t timestamp_ms = 0; // millis() when it arrived
};

// Engine speed (0x520 EngineSpeed) with the time its frame arrived. Whether
// it is the load-cell shaft is setup.same_shaft_confirmed; whether it is
// fresh is decided by computeTorquePower (limits.rpm_freshness_ms, which the
// firmware sets to CAN_FRESHNESS_MS, the same rule as EcuState::isFresh).
ShaftSpeedSample engineSpeedFromEcu(const EcuState& ecu);

// The mechanical setup. Real values come from DynoConfig.h; tests use a
// labelled fixture. Unconfirmed values make the dependent result invalid.
struct MechanicalSetup {
    double effective_arm_m;    // metres, perpendicular from shaft axis to the force's line
    bool geometry_confirmed;   // arm (and force direction) measured by the mechanical team
    bool same_shaft_confirmed; // the RPM really is the shaft the load cell measures
};

// Freshness and plausibility limits (DynoConfig.h holds the real ones).
struct CalcLimits {
    uint32_t force_freshness_ms; // force is usable while its age < this
    uint32_t rpm_freshness_ms;   // RPM is usable while its age < this
    uint32_t max_source_skew_ms; // force and RPM timestamps at most this far apart
    double max_arm_m;            // a longer arm is treated as a typo (e.g. mm entered as m)
};

// ---------------------------------------------------------------------------
// Result. When valid is false every value is NaN: never read it.

// Why a result is invalid. Checked in the order listed in docs/torque-power.md;
// the first failing check is reported. Power that is invalid because torque
// is invalid reports torque's reason (the root cause).
enum class CalcReason : uint8_t {
    None,                // valid
    ForceUnavailable,    // force.available is false (no sample, load-cell fault, no calibration)
    CalibrationNotReady, // force.calibration_ready is false (calibration/tare collecting)
    ForceNotFinite,      // force is NaN or infinite
    ForceStale,          // force age >= limits.force_freshness_ms
    GeometryUnconfirmed, // setup.geometry_confirmed is false
    ArmInvalid,          // arm <= 0, NaN, infinite, or > limits.max_arm_m
    // Power only (torque can still be valid):
    ShaftSpeedUnconfirmed, // setup.same_shaft_confirmed is false
    RpmMissing,          // speed.received is false (never received)
    RpmInvalid,          // RPM is NaN, infinite or negative
    RpmStale,            // RPM age >= limits.rpm_freshness_ms
    SourceSkewTooLarge,  // force and RPM timestamps more than limits.max_source_skew_ms apart
    Count
};

// "ForceStale" etc., for diagnostic lines. Never null.
const char* calcReasonName(CalcReason reason);

struct TorqueResult {
    bool valid = false;
    CalcReason reason = CalcReason::None;
    double torque_nm = NAN;
};

struct PowerResult {
    bool valid = false;
    CalcReason reason = CalcReason::None;
    double power_w = NAN;
    double power_kw = NAN;
    double horsepower_hp = NAN; // mechanical hp
};

struct TorquePowerResult {
    TorqueResult torque;
    PowerResult power;
    // Valid torque below zero: the load cell is pushed the "wrong" way, i.e.
    // the dyno is driving the engine, or the load cell was calibrated in the
    // opposite direction. The signed values are kept, never clamped; this
    // flags the unsupported operating state.
    bool reverse_torque = false;

    // Copied from the inputs as-is (meaningful only if that input was
    // available/received), so the consumer can report source ages.
    uint32_t force_timestamp_ms = 0;
    uint32_t rpm_timestamp_ms = 0;

    // The calibration/tare pair of the force this result was computed from,
    // never the "current" one. versions_known is true only when that force
    // was usable (available, calibration ready, finite, fresh); otherwise the
    // versions are blank, like the force.
    bool versions_known = false;
    uint32_t calibration_version = 0;
    uint32_t tare_version = 0;
};

// Take now_ms AFTER copying the inputs out of their mailboxes.
TorquePowerResult computeTorquePower(const ForceMeasurement& force, const ShaftSpeedSample& speed,
                                     const MechanicalSetup& setup, const CalcLimits& limits, uint32_t now_ms);
