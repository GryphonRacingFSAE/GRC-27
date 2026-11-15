#include "CsvRow.h"

#include <math.h>
#include <stdlib.h>

#include "LineBuilder.h"

DynoSnapshot takeSnapshot(const MeasurementInputs& in, const SnapshotConfig& config, uint32_t now_ms,
                          uint32_t output_dropped_rows) {
    DynoSnapshot s;
    s.timestamp_ms = now_ms;
    s.can_received_mask = in.ecu.receivedMask();
    s.can_fresh_mask = in.ecu.freshMask(now_ms, config.can_freshness_ms);
    for (size_t i = 0; i < kEcuSignalCount; ++i) {
        s.ecu_raw[i] = in.ecu.raw_values[i];
    }

    // Unsigned subtraction: right across a millis() wrap; a sample "from the
    // future" looks ~49 days old and is stale.
    s.loadcell_valid = in.loadcell.status == LoadCellStatus::Valid &&
                       (now_ms - in.loadcell.timestamp_ms) < config.loadcell_freshness_ms;
    s.loadcell_raw = in.loadcell.raw;

    s.calc = computeTorquePower(in.force, engineSpeedFromEcu(in.ecu), config.setup, config.limits, now_ms);
    s.force_valid = s.calc.versions_known;
    s.force_n = s.force_valid ? in.force.force_n : NAN;
    s.calibration_ready = in.force.calibration_ready;
    s.output_dropped_rows = output_dropped_rows;
    return s;
}

int ecuSignalDecimals(EcuSignal signal) {
    double scale = ecuSignalDef(signal).scale;
    int d = 0;
    while (d < 6 && scale < 0.999999) {
        scale *= 10;
        ++d;
    }
    return d;
}

// raw x 10^-decimals, printed exactly with integer maths (no float rounding):
// raw -1000, 3 decimals -> "-1.000".
static void appendScaled(LineBuilder& b, int32_t raw, int decimals) {
    if (decimals == 0) {
        b.append("%ld", static_cast<long>(raw));
        return;
    }
    long divisor = 1;
    for (int i = 0; i < decimals; ++i) divisor *= 10;
    const long long value = raw;
    const unsigned long long magnitude = static_cast<unsigned long long>(value < 0 ? -value : value);
    b.append("%s%llu.%0*llu", value < 0 ? "-" : "", magnitude / static_cast<unsigned long long>(divisor), decimals,
             magnitude % static_cast<unsigned long long>(divisor));
}

static EcuSignal ecuSignalFor(CsvColumn column) {
    switch (column) {
    case CsvColumn::Rpm: return EcuSignal::EngineSpeed;
    case CsvColumn::LambdaAvg: return EcuSignal::LambdaAverage;
    case CsvColumn::LambdaA: return EcuSignal::LambdaA;
    case CsvColumn::LambdaB: return EcuSignal::LambdaB;
    case CsvColumn::FuelInjPulseWidthMs: return EcuSignal::FuelInjPulsewidth;
    case CsvColumn::FuelInjDutyPct: return EcuSignal::FuelInjDuty;
    case CsvColumn::KnockDetected: return EcuSignal::KnockDetected;
    case CsvColumn::RevLimitRpm: return EcuSignal::RevLimitRpm;
    case CsvColumn::LambdaTarget: return EcuSignal::LambdaTarget;
    case CsvColumn::BatteryV: return EcuSignal::BatteryVoltage;
    case CsvColumn::IntakeAirTempC: return EcuSignal::IntakeAirTemp;
    case CsvColumn::CoolantTempC: return EcuSignal::CoolantTemp;
    case CsvColumn::Gear: return EcuSignal::GearPosn;
    default: return EcuSignal::Count;
    }
}

static void appendEcuCell(LineBuilder& b, const DynoSnapshot& s, EcuSignal signal) {
    const EcuSignalDef& def = ecuSignalDef(signal);
    const uint8_t bit = static_cast<uint8_t>(1u << static_cast<unsigned>(def.message));
    if (s.can_fresh_mask & bit) {
        appendScaled(b, s.ecu_raw[static_cast<size_t>(signal)], ecuSignalDecimals(signal));
    }
}

// A calculated value: blank unless valid. %.*f of a finite double.
static void appendReal(LineBuilder& b, bool valid, double value, int decimals) {
    if (valid && isfinite(value)) {
        b.append("%.*f", decimals, value);
    }
}

// Precision of the calculated columns (documented in docs/csv-output.md).
static constexpr int kForceDecimals = 3;  // N
static constexpr int kTorqueDecimals = 3; // N.m
static constexpr int kPowerDecimals = 1;  // W
static constexpr int kHpDecimals = 3;     // mechanical hp

bool formatCsvRow(const DynoSnapshot& s, OutputLine& line, size_t capacity) {
    LineBuilder b(line, capacity);
    for (size_t i = 0; i < kCsvColumnCount; ++i) {
        if (i) b.append(",");
        const CsvColumn column = kCsvColumns[i].column;
        switch (column) {
        case CsvColumn::SchemaVersion:
            b.append("%lu", static_cast<unsigned long>(kCsvSchemaVersion));
            break;
        case CsvColumn::TimestampMs:
            b.append("%lu", static_cast<unsigned long>(s.timestamp_ms));
            break;
        case CsvColumn::CanReceivedMask:
            b.append("%u", static_cast<unsigned>(s.can_received_mask));
            break;
        case CsvColumn::CanFreshMask:
            b.append("%u", static_cast<unsigned>(s.can_fresh_mask));
            break;
        case CsvColumn::Rpm:
        case CsvColumn::LambdaAvg:
        case CsvColumn::LambdaA:
        case CsvColumn::LambdaB:
        case CsvColumn::FuelInjPulseWidthMs:
        case CsvColumn::FuelInjDutyPct:
        case CsvColumn::KnockDetected:
        case CsvColumn::RevLimitRpm:
        case CsvColumn::LambdaTarget:
        case CsvColumn::BatteryV:
        case CsvColumn::IntakeAirTempC:
        case CsvColumn::CoolantTempC:
        case CsvColumn::Gear:
            appendEcuCell(b, s, ecuSignalFor(column));
            break;
        case CsvColumn::LoadcellRaw:
            if (s.loadcell_valid) b.append("%ld", static_cast<long>(s.loadcell_raw));
            break;
        case CsvColumn::ForceN:
            appendReal(b, s.force_valid, s.force_n, kForceDecimals);
            break;
        case CsvColumn::TorqueNm:
            appendReal(b, s.calc.torque.valid, s.calc.torque.torque_nm, kTorqueDecimals);
            break;
        case CsvColumn::PowerW:
            appendReal(b, s.calc.power.valid, s.calc.power.power_w, kPowerDecimals);
            break;
        case CsvColumn::HorsepowerHp:
            appendReal(b, s.calc.power.valid, s.calc.power.horsepower_hp, kHpDecimals);
            break;
        case CsvColumn::LoadcellValid:
            b.append("%d", s.loadcell_valid ? 1 : 0);
            break;
        case CsvColumn::CalibrationReady:
            b.append("%d", s.calibration_ready ? 1 : 0);
            break;
        case CsvColumn::CalibrationVersion:
            if (s.calc.versions_known) b.append("%lu", static_cast<unsigned long>(s.calc.calibration_version));
            break;
        case CsvColumn::TareVersion:
            if (s.calc.versions_known) b.append("%lu", static_cast<unsigned long>(s.calc.tare_version));
            break;
        case CsvColumn::OutputDroppedRows:
            b.append("%lu", static_cast<unsigned long>(s.output_dropped_rows));
            break;
        case CsvColumn::Count:
            break;
        }
    }
    b.append("\r\n");
    return b.finish();
}
