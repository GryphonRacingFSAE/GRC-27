// ============================================================================
// The dyno's serial CSV (#38): THE one definition of the columns. The header
// and the row formatter both walk kCsvColumns, so their order cannot drift.
// See docs/csv-output.md for the column dictionary.
//
// To change the schema: edit CsvColumn + kCsvColumns together, handle the new
// column in formatCsvRow (CsvRow.cpp) (the compiler warns about a missing switch
// case), bump kCsvSchemaVersion, and update docs/csv-output.md and
// docs/csv-sample.csv.
// ============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "OutputLine.h"

static constexpr uint32_t kCsvSchemaVersion = 1;

enum class CsvColumn : uint8_t {
    SchemaVersion,
    TimestampMs,
    CanReceivedMask,
    CanFreshMask,
    Rpm,
    LambdaAvg,
    LambdaA,
    LambdaB,
    FuelInjPulseWidthMs,
    FuelInjDutyPct,
    KnockDetected,
    RevLimitRpm,
    LambdaTarget,
    BatteryV,
    IntakeAirTempC,
    CoolantTempC,
    Gear,
    LoadcellRaw,
    ForceN,
    TorqueNm,
    PowerW,
    HorsepowerHp,
    LoadcellValid,
    CalibrationReady,
    CalibrationVersion,
    TareVersion,
    OutputDroppedRows,
    Count
};

static constexpr size_t kCsvColumnCount = static_cast<size_t>(CsvColumn::Count);

struct CsvColumnDef {
    CsvColumn column;
    const char* name;
};

// Rows must be in enum order (checked below).
inline constexpr CsvColumnDef kCsvColumns[] = {
    {CsvColumn::SchemaVersion,       "schema_version"},
    {CsvColumn::TimestampMs,         "timestamp_ms"},
    {CsvColumn::CanReceivedMask,     "can_received_mask"},
    {CsvColumn::CanFreshMask,        "can_fresh_mask"},
    {CsvColumn::Rpm,                 "rpm"},
    {CsvColumn::LambdaAvg,           "lambda_avg"},
    {CsvColumn::LambdaA,             "lambda_a"},
    {CsvColumn::LambdaB,             "lambda_b"},
    {CsvColumn::FuelInjPulseWidthMs, "fuel_inj_pulse_width_ms"},
    {CsvColumn::FuelInjDutyPct,      "fuel_inj_duty_pct"},
    {CsvColumn::KnockDetected,       "knock_detected"},
    {CsvColumn::RevLimitRpm,         "rev_limit_rpm"},
    {CsvColumn::LambdaTarget,        "lambda_target"},
    {CsvColumn::BatteryV,            "battery_v"},
    {CsvColumn::IntakeAirTempC,      "intake_air_temp_c"},
    {CsvColumn::CoolantTempC,        "coolant_temp_c"},
    {CsvColumn::Gear,                "gear"},
    {CsvColumn::LoadcellRaw,         "loadcell_raw"},
    {CsvColumn::ForceN,              "force_n"},
    {CsvColumn::TorqueNm,            "torque_nm"},
    {CsvColumn::PowerW,              "power_w"},
    {CsvColumn::HorsepowerHp,        "horsepower_hp"},
    {CsvColumn::LoadcellValid,       "loadcell_valid"},
    {CsvColumn::CalibrationReady,    "calibration_ready"},
    {CsvColumn::CalibrationVersion,  "calibration_version"},
    {CsvColumn::TareVersion,         "tare_version"},
    {CsvColumn::OutputDroppedRows,   "output_dropped_rows"},
};

constexpr bool csvColumnsAreConsistent() {
    if (sizeof(kCsvColumns) / sizeof(kCsvColumns[0]) != kCsvColumnCount) return false;
    for (size_t i = 0; i < kCsvColumnCount; ++i) {
        if (static_cast<size_t>(kCsvColumns[i].column) != i) return false;
    }
    return true;
}
static_assert(csvColumnsAreConsistent(), "CsvSchema.h: kCsvColumns out of CsvColumn order");
static_assert(kCsvColumnCount == 27, "CsvSchema.h: issue #38 defines exactly 27 columns");

// The header line ("schema_version,...,output_dropped_rows\r\n").
// Returns false (and an empty line) if it doesn't fit; it always does.
bool formatCsvHeader(OutputLine& line);
