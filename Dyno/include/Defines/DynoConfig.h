// ============================================================================
// Dyno configuration: tunable settings (and, later, the mechanical setup).
// Each value lives only here; nothing else may hard-code these numbers.
// Later tickets add their own sections (CAN, calibration, geometry, CSV).
// ============================================================================
#pragma once

#include <stdint.h>

// ----------------------------------------------------------------------------
// HX711 acquisition. Measure/confirm on hardware later; valid defaults now.
// ----------------------------------------------------------------------------
static constexpr uint32_t HX711_READY_DEADLINE_MS    = 150;   // > 100 ms, so it works at either 10 Hz or 80 Hz
static constexpr uint32_t HX711_RETRY_LIMIT          = 3;
static constexpr uint32_t HX711_RECOVERY_INTERVAL_MS = 1000;

static_assert(HX711_READY_DEADLINE_MS > 100, "DynoConfig.h: HX711 deadline must cover a 10 Hz conversion");
