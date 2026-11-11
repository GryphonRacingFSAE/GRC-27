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
static constexpr uint32_t HX711_POLL_INTERVAL_MS     = 5;     // how often the task asks "ready?"

static_assert(HX711_READY_DEADLINE_MS > 100, "DynoConfig.h: HX711 deadline must cover a 10 Hz conversion");
static_assert(HX711_RETRY_LIMIT >= 1, "DynoConfig.h: HX711 needs at least one try before a Fault");
static_assert(HX711_POLL_INTERVAL_MS < 12, "DynoConfig.h: HX711 poll must beat 80 Hz (12.5 ms) or readings are skipped");

// ----------------------------------------------------------------------------
// Build switches. Override from platformio.ini with -D<NAME>=0/1.
// ----------------------------------------------------------------------------
#ifndef DYNO_FAKE_INPUTS
#define DYNO_FAKE_INPUTS 0       // 1 = scripted fake load cell instead of the HX711 (bare devkit)
#endif
#ifndef DYNO_LOADCELL_MONITOR
#define DYNO_LOADCELL_MONITOR 1  // temporary "# loadcell ..." serial printout; removed by #38
#endif

static constexpr uint32_t LOADCELL_MONITOR_INTERVAL_MS = 500;   // 2 Hz printout
static constexpr uint32_t DIAGNOSTIC_REPEAT_INTERVAL_MS = 10000; // re-print startup warnings
