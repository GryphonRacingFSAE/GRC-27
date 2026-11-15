// The calibration settings the host tests are written against (#35). They
// equal today's DynoConfig.h values (test_config checks this), but are kept
// here so the tests' hand-worked timings don't silently change meaning when
// the real settings are retuned: a retune fails test_config, and then these
// tests' timings are reworked on purpose.
#pragma once

#include "LoadCellCalibrator.h"

// 1 s window, >= 8 samples, spread <= 500 counts, 10 s timeout, saturated at
// |raw| >= 8388000; loaded and unloaded at least 5000 counts apart.
static const CalibratorConfig kExampleCalibratorConfig = {{1000, 8, 500, 10000, 8388000}, 5000.0};
