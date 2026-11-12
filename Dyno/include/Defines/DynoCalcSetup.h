// The REAL torque/power setup and limits (#37), built from DynoConfig.h in one
// place. The firmware and test_config both use these, so the tests check
// exactly what the firmware runs. (Host tests of the maths use the labelled
// fixture in TorquePowerExamples.h instead.)
#pragma once

#include <TorquePower.h>

#include "DynoConfig.h"

inline MechanicalSetup dynoMechanicalSetup() {
    return {EFFECTIVE_ARM_M, GEOMETRY_CONFIRMED, SAME_SHAFT_CONFIRMED};
}

// RPM freshness is the ECU's CAN freshness (#33), the same rule as EcuState::isFresh.
inline CalcLimits dynoCalcLimits() {
    return {FORCE_FRESHNESS_MS, CAN_FRESHNESS_MS, MAX_SOURCE_SKEW_MS, EFFECTIVE_ARM_MAX_M};
}
