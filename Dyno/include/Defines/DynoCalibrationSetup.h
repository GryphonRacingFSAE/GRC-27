// The REAL calibration settings (#35), built from DynoConfig.h in one place.
// The firmware and test_config both use this, so the tests check exactly what
// the firmware runs. (Host tests use the fixture in CalibrationExamples.h.)
#pragma once

#include <LoadCellCalibrator.h>

#include "DynoConfig.h"

inline CalibratorConfig dynoCalibratorConfig() {
    CalibratorConfig c;
    c.window = {CAL_WINDOW_MS, CAL_MIN_SAMPLES, CAL_MAX_SPREAD_COUNTS, CAL_TIMEOUT_MS, LOADCELL_SATURATION_COUNTS};
    c.min_separation_counts = CAL_MIN_SEPARATION_COUNTS;
    return c;
}
