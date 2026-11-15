// Load-cell calibration maths (#35): raw HX711 counts <-> force in newtons.
// Pure functions, no hardware, no clock. See docs/calibration.md.
//
// Convention (issue #35):
//   counts_per_N = (loaded_counts - unloaded_counts) / reference_force_N
//   force_N      = (raw_counts - offset_counts) / counts_per_N
// The offset is the unloaded reading (tare replaces it). The scale is signed:
// a load cell wired "backwards" gives a negative scale, which is valid, so
// force keeps the direction the reference load was applied in.
#pragma once

#include <math.h>
#include <stdint.h>

// The accepted calibration. Plain data (copied whole, never half-updated).
// Only usable if `valid` AND the scale is finite and non-zero: a default
// (all zeros) record is never a calibration.
struct CalibrationRecord {
    bool valid = false;
    double offset_counts = 0.0; // raw counts at zero force (from the calibration, or the latest tare)
    double counts_per_n = 0.0;  // signed scale, raw counts per newton

    // Notes on how it was measured (not used for conversion).
    double reference_force_n = NAN;   // the known load, newtons
    double reference_mass_kg = NAN;   // if it was given as a mass; NAN if given in newtons
    double gravity_m_s2 = NAN;        // g used for mass -> force; NAN if given in newtons
    double unloaded_counts = NAN;     // mean of the unloaded window
    double loaded_counts = NAN;       // mean of the loaded window

    // The calibration/tare version pair (issue #35 "shared contract"):
    // calibration_version +1 on each accepted known-load calibration;
    // tare_version +1 on each accepted change of offset (calibration or tare).
    // 32-bit, wrapping after ~4 billion changes. 0/0 = never calibrated.
    uint32_t calibration_version = 0;
    uint32_t tare_version = 0;
};

enum class CalibrationError : uint8_t {
    None,
    ReferenceForceInvalid, // zero, negative, NaN or infinite reference force
    CountsInvalid,         // NaN or infinite unloaded/loaded counts
    SeparationTooSmall,    // |loaded - unloaded| below the minimum: can't tell load from noise
    ScaleInvalid,          // resulting scale zero or not finite
};

struct ScaleResult {
    CalibrationError error = CalibrationError::None;
    double counts_per_n = 0.0; // only meaningful when error == None
};

// The signed scale from an unloaded and a loaded reading. The reference force
// must be positive: the direction comes from the counts, never from the sign
// of the typed-in load.
ScaleResult scaleFromPoints(double unloaded_counts, double loaded_counts, double reference_force_n,
                            double min_separation_counts);

struct ForceConversion {
    bool valid = false;
    double force_n = NAN; // NAN unless valid
};

// Force for one raw reading. Invalid (never 0 N) if the record isn't usable.
ForceConversion forceFromRaw(const CalibrationRecord& record, int32_t raw_counts);

// Is this record usable for conversion? (valid, finite offset, finite non-zero scale)
bool calibrationUsable(const CalibrationRecord& record);

// Standard gravity (BIPM/NIST), used to turn a reference mass into a force.
static constexpr double kStandardGravityMs2 = 9.80665;

// F = m * g. Mass in kilograms, g in m/s^2, result in newtons.
double newtonsFromKilograms(double mass_kg, double gravity_m_s2);

const char* calibrationErrorName(CalibrationError error);
