// ============================================================================
// Dyno configuration: tunable settings (and, later, the mechanical setup).
// Each value lives only here; nothing else may hard-code these numbers.
// Later tickets add their own sections.
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
// Mechanical setup for torque/power (#37). See docs/torque-power.md.
//
// TBD: NOT measured yet. Ask the mechanical team for the setup note in
// docs/torque-power.md, then change the numbers and flip the flags. No other
// code changes are needed. Until a flag is true, the result that depends on
// it is INVALID with a reason (never a convincing-looking guess):
//   GEOMETRY_CONFIRMED   = false -> no torque and no power
//   SAME_SHAFT_CONFIRMED = false -> no power (torque can still be valid)
// ----------------------------------------------------------------------------
static constexpr bool   GEOMETRY_CONFIRMED   = false; // TBD: arm and force direction measured
static constexpr double EFFECTIVE_ARM_M      = 0.0;   // TBD: METRES, perpendicular from shaft axis to the load cell's line of force
static constexpr bool   SAME_SHAFT_CONFIRMED = false; // TBD: ECU engine RPM is the shaft the load cell measures

// Limits (valid now; retune on hardware). If changed, rework the boundary
// rows in lib/DynoCore/src/TorquePowerExamples.h (test_config checks they match).
static constexpr uint32_t FORCE_FRESHNESS_MS  = 500;  // force usable while age < this (10 Hz = 100 ms/sample; Fault after ~450 ms)
static constexpr uint32_t MAX_SOURCE_SKEW_MS  = 250;  // force and RPM timestamps at most this far apart for power
static constexpr double   EFFECTIVE_ARM_MAX_M = 2.0;  // typo guard: a longer "arm" is rejected (e.g. 250 mm typed as 250)

static_assert(EFFECTIVE_ARM_MAX_M > 0.0, "DynoConfig.h: the arm limit must be positive");
static_assert(!GEOMETRY_CONFIRMED || (EFFECTIVE_ARM_M > 0.0 && EFFECTIVE_ARM_M <= EFFECTIVE_ARM_MAX_M),
              "DynoConfig.h: GEOMETRY_CONFIRMED needs a real EFFECTIVE_ARM_M in metres (0 < arm <= EFFECTIVE_ARM_MAX_M)");
static_assert(FORCE_FRESHNESS_MS > 100, "DynoConfig.h: force freshness must cover a 10 Hz HX711 sample period");
static_assert(MAX_SOURCE_SKEW_MS > 0 && MAX_SOURCE_SKEW_MS < CAN_FRESHNESS_MS,
              "DynoConfig.h: source skew limit must be positive and shorter than the CAN freshness");

// ----------------------------------------------------------------------------
// Load-cell calibration and tare (#35). See docs/calibration.md.
//
// TBD: starting values, not yet checked against the real load cell's noise.
// Retune them on the rig (docs/calibration.md, "Tuning"). If changed, rework
// the fixture in lib/DynoCore/src/CalibrationExamples.h (test_config checks
// they match).
// ----------------------------------------------------------------------------
// A reading is stable when the last CAL_WINDOW_MS of samples (at least
// CAL_MIN_SAMPLES of them) differ by at most CAL_MAX_SPREAD_COUNTS; the
// result is their mean. Give up CAL_TIMEOUT_MS after the command.
static constexpr uint32_t CAL_WINDOW_MS             = 1000;   // TBD
static constexpr uint32_t CAL_MIN_SAMPLES           = 8;      // TBD: 10 Hz gives 10 per second
static constexpr int32_t  CAL_MAX_SPREAD_COUNTS     = 500;    // TBD: measure the unloaded noise first
static constexpr uint32_t CAL_TIMEOUT_MS            = 10000;  // TBD
// loaded - unloaded must differ by at least this, or noise would decide the scale.
static constexpr double   CAL_MIN_SEPARATION_COUNTS = 5000.0; // TBD
// A raw reading this close to the HX711's 24-bit limit (+/-8388607) is
// treated as maxed out (overloaded, or the bridge unplugged).
static constexpr int32_t  LOADCELL_SATURATION_COUNTS = 8388000; // TBD

static_assert(CAL_MIN_SAMPLES >= 2 && CAL_MIN_SAMPLES <= CAL_WINDOW_MS / 100,
              "DynoConfig.h: a 10 Hz HX711 must be able to fill CAL_MIN_SAMPLES within CAL_WINDOW_MS");
static_assert(CAL_WINDOW_MS * 80 / 1000 <= 128,
              "DynoConfig.h: CAL_WINDOW_MS at 80 Hz must fit StableWindow::kMaxSamples (128)");
static_assert(CAL_TIMEOUT_MS >= 2 * CAL_WINDOW_MS, "DynoConfig.h: the timeout must allow time to settle first");
static_assert(CAL_MAX_SPREAD_COUNTS > 0, "DynoConfig.h: the stability spread must be positive");
static_assert(CAL_MIN_SEPARATION_COUNTS > CAL_MAX_SPREAD_COUNTS,
              "DynoConfig.h: the minimum separation must be larger than the allowed noise");
static_assert(LOADCELL_SATURATION_COUNTS > 0 && LOADCELL_SATURATION_COUNTS <= 8388607,
              "DynoConfig.h: saturation must be within the HX711's 24-bit range");

// Serial command input (CommandTask). Not tuning values: these only bound
// how much work typing can cause.
static constexpr uint32_t COMMAND_POLL_INTERVAL_MS  = 20; // how often typed bytes are read
static constexpr uint32_t COMMAND_MAX_BYTES_PER_POLL = 64; // the rest wait for the next poll
static constexpr uint32_t COMMAND_QUEUE_LENGTH      = 4;  // parsed commands waiting for LoadCellTask
static_assert(COMMAND_MAX_BYTES_PER_POLL > 0 && COMMAND_QUEUE_LENGTH > 0,
              "DynoConfig.h: command input needs room for at least one byte and one command");

// Tare button (#36), polled by CommandTask every COMMAND_POLL_INTERVAL_MS.
// A press counts once the pin has read "pressed" this long. TBD: check the
// real button's bounce on the rig (docs/calibration.md, "Tare button").
static constexpr uint32_t TARE_DEBOUNCE_MS = 50; // TBD
static_assert(TARE_DEBOUNCE_MS >= 2 * COMMAND_POLL_INTERVAL_MS && TARE_DEBOUNCE_MS <= 150,
              "DynoConfig.h: tare debounce must span two button polls and stay under 150 ms");

// ----------------------------------------------------------------------------
// Build switches. Override from platformio.ini with -D<NAME>=0/1.
// ----------------------------------------------------------------------------
#ifndef DYNO_FAKE_INPUTS
#define DYNO_FAKE_INPUTS 0       // 1 = scripted fake load cell and ECU instead of the HX711 and CAN bus (bare devkit)
#endif

// ----------------------------------------------------------------------------
// Serial CSV output (#38). See docs/csv-output.md.
// ----------------------------------------------------------------------------
// TBD (team decision still to be made): how often a CSV row is printed.
// 100 ms = 10 rows/s for now. Change this one number to change the rate;
// nothing else needs editing (the static_asserts below keep it sane).
static constexpr uint32_t CSV_ROW_INTERVAL_MS = 100;

static constexpr uint32_t CSV_OUTPUT_QUEUE_LINES = 16;     // lines waiting for the serial port (~1.6 s of rows); full = new row dropped + counted
static constexpr uint32_t STATUS_LINE_INTERVAL_MS = 5000;  // "# loadcell/# can/# calc/# output" status lines
static constexpr uint32_t DIAGNOSTIC_REPEAT_INTERVAL_MS = 10000; // re-print startup warnings (USB serial often misses boot)

// The serial speed, defined once in platformio.ini ([dyno] serial_baud) and
// passed in as -DDYNO_SERIAL_BAUD so the monitor and the firmware agree.
// (Over the ESP32-S3's native USB the number isn't used by the hardware, but
// it is if the board is read through a UART adapter.)
#ifndef DYNO_SERIAL_BAUD
#define DYNO_SERIAL_BAUD 115200
#endif

static_assert(CSV_ROW_INTERVAL_MS >= 20 && CSV_ROW_INTERVAL_MS <= 1000,
              "DynoConfig.h: CSV row interval must be 20-1000 ms");
static_assert(CSV_ROW_INTERVAL_MS < CAN_FRESHNESS_MS && CSV_ROW_INTERVAL_MS < FORCE_FRESHNESS_MS,
              "DynoConfig.h: rows must come faster than inputs go stale, or fresh data is never printed");
// A worst-case row (~256 bytes, 10 bits per byte on a UART) must fit the line rate.
static_assert(256ull * 10 * (1000 / CSV_ROW_INTERVAL_MS) < DYNO_SERIAL_BAUD,
              "DynoConfig.h: serial baud too slow for this CSV row interval");
static_assert(CSV_OUTPUT_QUEUE_LINES >= 4, "DynoConfig.h: output queue needs room for a row plus status lines");
static_assert(STATUS_LINE_INTERVAL_MS >= 1000, "DynoConfig.h: status lines are low-rate (>= 1 s) so rows dominate");
