// Turning the producers' latest data into one CSV measurement row (#38).
// Pure: no hardware, no clock, no RTOS. See docs/csv-output.md.
//
//   1. Copy the producers' state out of their mailboxes into MeasurementInputs.
//   2. Read the clock (AFTER the copies, so no age is negative).
//   3. takeSnapshot(): decides once what is fresh/valid and computes
//      torque/power, freezing values, readiness, timestamps and the
//      calibration/tare version pair together.
//   4. formatCsvRow(): the snapshot as text, in kCsvColumns order.
//
// The row is text before it is queued, so a queued row can never pick up
// newer calibration metadata.
#pragma once

#include <stdint.h>

#include "CsvSchema.h"
#include "EcuState.h"
#include "ForceMeasurement.h"
#include "LoadCellSample.h"
#include "OutputLine.h"
#include "TorquePower.h"

// Copies of what the producers published. Defaults = nothing yet (no CAN,
// no load-cell sample, force unavailable): every measurement cell is blank.
struct MeasurementInputs {
    EcuState ecu;              // #33
    LoadCellSample loadcell;   // #34
    ForceMeasurement force;    // #35 (until then: always unavailable)
};

// Thresholds and the mechanical setup. The firmware builds the real one in
// include/Defines/DynoCalcSetup.h; tests use a labelled fixture.
struct SnapshotConfig {
    uint32_t can_freshness_ms = 0;      // an ECU message is fresh while its age < this
    uint32_t loadcell_freshness_ms = 0; // a raw load-cell sample is fresh while its age < this
    MechanicalSetup setup = {0.0, false, false};
    CalcLimits limits = {0, 0, 0, 0.0};
};

// One frozen row. Every "valid" decision was made when it was taken.
struct DynoSnapshot {
    uint32_t timestamp_ms = 0; // millis() when the snapshot was taken (wraps after ~49.7 days)
    uint8_t can_received_mask = 0;
    uint8_t can_fresh_mask = 0;
    int32_t ecu_raw[kEcuSignalCount] = {}; // DBC raw values; a cell is printed only if its message is fresh

    bool loadcell_valid = false; // a fresh, Valid raw sample
    int32_t loadcell_raw = 0;

    // Force is printed only when it is usable: available, calibration ready,
    // finite and fresh. That is exactly when #37 reports versions_known, so
    // the force cell and the version cells are always blank together.
    bool force_valid = false;
    double force_n = NAN;
    TorquePowerResult calc; // torque/power/hp and the version pair of the force used

    bool calibration_ready = false; // as copied with this snapshot's force
    uint32_t output_dropped_rows = 0;
};

DynoSnapshot takeSnapshot(const MeasurementInputs& in, const SnapshotConfig& config, uint32_t now_ms,
                          uint32_t output_dropped_rows);

// The snapshot as a CSV row ending in CRLF. Returns false, and leaves the
// line EMPTY, if the row doesn't fit in `capacity` characters (never a
// truncated row). The firmware always uses the full OutputLine capacity;
// a smaller one is only for testing the overflow path.
bool formatCsvRow(const DynoSnapshot& snap, OutputLine& line, size_t capacity = OutputLine::kCapacity);

// Decimal places of an ECU signal in the CSV: its DBC resolution
// (scale 0.001 -> 3, scale 1 -> 0).
int ecuSignalDecimals(EcuSignal signal);
