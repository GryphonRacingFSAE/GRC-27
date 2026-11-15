#include "CalibrationMath.h"

ScaleResult scaleFromPoints(double unloaded_counts, double loaded_counts, double reference_force_n,
                            double min_separation_counts) {
    ScaleResult r;
    if (!isfinite(reference_force_n) || reference_force_n <= 0.0) {
        r.error = CalibrationError::ReferenceForceInvalid;
        return r;
    }
    if (!isfinite(unloaded_counts) || !isfinite(loaded_counts)) {
        r.error = CalibrationError::CountsInvalid;
        return r;
    }
    const double difference = loaded_counts - unloaded_counts;
    if (fabs(difference) < min_separation_counts) {
        r.error = CalibrationError::SeparationTooSmall;
        return r;
    }
    const double scale = difference / reference_force_n;
    if (!isfinite(scale) || scale == 0.0) {
        r.error = CalibrationError::ScaleInvalid;
        return r;
    }
    r.counts_per_n = scale;
    return r;
}

bool calibrationUsable(const CalibrationRecord& record) {
    return record.valid && isfinite(record.offset_counts) && isfinite(record.counts_per_n) &&
           record.counts_per_n != 0.0;
}

ForceConversion forceFromRaw(const CalibrationRecord& record, int32_t raw_counts) {
    ForceConversion f;
    if (!calibrationUsable(record)) {
        return f;
    }
    const double force = (static_cast<double>(raw_counts) - record.offset_counts) / record.counts_per_n;
    if (!isfinite(force)) {
        return f;
    }
    f.valid = true;
    f.force_n = force;
    return f;
}

double newtonsFromKilograms(double mass_kg, double gravity_m_s2) { return mass_kg * gravity_m_s2; }

const char* calibrationErrorName(CalibrationError error) {
    switch (error) {
    case CalibrationError::None:
        return "None";
    case CalibrationError::ReferenceForceInvalid:
        return "ReferenceForceInvalid";
    case CalibrationError::CountsInvalid:
        return "CountsInvalid";
    case CalibrationError::SeparationTooSmall:
        return "SeparationTooSmall";
    case CalibrationError::ScaleInvalid:
        return "ScaleInvalid";
    }
    return "?";
}
