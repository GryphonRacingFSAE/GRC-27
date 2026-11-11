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
// ECU CAN reception (#33). See docs/can-decoding.md.
// ----------------------------------------------------------------------------
static constexpr uint32_t CAN_BITRATE_KBPS        = 500;   // confirmed: GRC26 Dyno + GRC26 TELEMETRY.md (same MaxxECU)
static constexpr uint32_t CAN_FRESHNESS_MS        = 2000;  // team convention (GRC26 RTU); refine per message after measuring ECU rates
static constexpr uint32_t CAN_RX_TIMEOUT_MS       = 20;    // longest single wait for a frame (bounded, never forever)
static constexpr uint32_t CAN_IDLE_DELAY_MS       = 10;    // sleep after a step that got nothing (no busy loop)
static constexpr uint32_t CAN_RESTART_INTERVAL_MS = 1000;  // retry a failed driver start this often
static constexpr uint32_t CAN_RX_QUEUE_LEN        = 32;    // driver receive queue (frames); overflow is counted

static_assert(CAN_BITRATE_KBPS == 125 || CAN_BITRATE_KBPS == 250 || CAN_BITRATE_KBPS == 500 ||
                  CAN_BITRATE_KBPS == 1000,
              "DynoConfig.h: CAN bitrate must be one of 125/250/500/1000 kbit/s");
static_assert(CAN_FRESHNESS_MS > CAN_RX_TIMEOUT_MS + CAN_IDLE_DELAY_MS,
              "DynoConfig.h: CAN freshness must be longer than one receive loop");

// ----------------------------------------------------------------------------
// Build switches. Override from platformio.ini with -D<NAME>=0/1.
// ----------------------------------------------------------------------------
#ifndef DYNO_FAKE_INPUTS
#define DYNO_FAKE_INPUTS 0       // 1 = scripted fake load cell and ECU instead of the HX711 and CAN bus (bare devkit)
#endif
#ifndef DYNO_SERIAL_MONITOR
#define DYNO_SERIAL_MONITOR 1    // temporary "# loadcell/# ecu/# can ..." serial printout; removed by #38
#endif

static constexpr uint32_t SERIAL_MONITOR_INTERVAL_MS = 500;     // 2 Hz printout
static constexpr uint32_t DIAGNOSTIC_REPEAT_INTERVAL_MS = 10000; // re-print startup warnings
