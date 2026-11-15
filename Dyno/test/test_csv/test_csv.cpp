// CSV schema and row formatting (#38).
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "CsvRow.h"
#include "CsvSchema.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

// The header from issue #38, verbatim.
static const char* kIssueHeader =
    "schema_version,timestamp_ms,can_received_mask,can_fresh_mask,rpm,lambda_avg,lambda_a,lambda_b,"
    "fuel_inj_pulse_width_ms,fuel_inj_duty_pct,knock_detected,rev_limit_rpm,lambda_target,battery_v,"
    "intake_air_temp_c,coolant_temp_c,gear,loadcell_raw,force_n,torque_nm,power_w,horsepower_hp,"
    "loadcell_valid,calibration_ready,calibration_version,tare_version,output_dropped_rows\r\n";

static void test_header_matches_issue_with_crlf() {
    OutputLine line;
    TEST_ASSERT_TRUE(formatCsvHeader(line));
    TEST_ASSERT_EQUAL_STRING(kIssueHeader, line.text);
    TEST_ASSERT_EQUAL_UINT(strlen(kIssueHeader), line.length);
}

// ---------------------------------------------------------------------------
// Row helpers. Rows are checked cell by cell, by column NAME, so a test reads
// like the column dictionary and doesn't depend on counting commas.

// Fixture config: the agreed freshness limits and #37's example geometry
// (0.25 m, same shaft; FIXTURE ONLY, not the real dyno).
static SnapshotConfig fixtureConfig() {
    SnapshotConfig c;
    c.can_freshness_ms = 2000;
    c.loadcell_freshness_ms = 500;
    c.setup = {0.25, true, true};
    c.limits = kExampleLimits;
    return c;
}

static std::vector<std::string> splitCells(const OutputLine& line) {
    std::string text(line.text, line.length);
    TEST_ASSERT_TRUE_MESSAGE(text.size() >= 2 && text.compare(text.size() - 2, 2, "\r\n") == 0,
                             "line must end with CRLF");
    text.resize(text.size() - 2);
    std::vector<std::string> cells;
    size_t start = 0;
    for (;;) {
        const size_t comma = text.find(',', start);
        cells.push_back(text.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return cells;
}

static size_t columnIndex(const char* name) {
    for (size_t i = 0; i < kCsvColumnCount; ++i) {
        if (strcmp(kCsvColumns[i].name, name) == 0) return i;
    }
    TEST_FAIL_MESSAGE(name);
    return 0;
}

struct Row {
    std::vector<std::string> cells;
    const std::string& operator[](const char* name) const { return cells.at(columnIndex(name)); }
};

static Row formatRow(const MeasurementInputs& in, uint32_t now_ms, uint32_t dropped_rows = 0,
                     const SnapshotConfig& config = fixtureConfig()) {
    const DynoSnapshot snap = takeSnapshot(in, config, now_ms, dropped_rows);
    OutputLine line;
    TEST_ASSERT_TRUE(formatCsvRow(snap, line));
    Row row{splitCells(line)};
    TEST_ASSERT_EQUAL_UINT_MESSAGE(kCsvColumnCount, row.cells.size(), line.text);
    return row;
}

// ---------------------------------------------------------------------------
// Startup: nothing received, nothing measured, nothing calibrated.

static void test_startup_row_is_blank_except_fixed_columns() {
    const MeasurementInputs nothing; // default: no CAN, NoSampleYet, force unavailable
    const DynoSnapshot snap = takeSnapshot(nothing, fixtureConfig(), 0, 0);
    OutputLine line;
    TEST_ASSERT_TRUE(formatCsvRow(snap, line));
    //                       schema,ts,rx,fresh, 13 ECU,  raw..hp(5), lc_valid,cal_ready, versions, dropped
    TEST_ASSERT_EQUAL_STRING("1,0,0,0," ",,,,,,,,,,,,," ",,,,," "0,0," ",," "0\r\n", line.text);
}

// ---------------------------------------------------------------------------
// Input builders

static void setRaw(EcuState& ecu, EcuSignal sig, int32_t raw) {
    ecu.raw_values[static_cast<size_t>(sig)] = raw;
}

static void receiveAll(EcuState& ecu, uint32_t rx_ms) {
    for (auto& m : ecu.messages) {
        m.received = true;
        m.last_rx_ms = rx_ms;
    }
}

// A plausible value on every one of the 13 signals (raw DBC numbers).
static void setTypicalEcu(EcuState& ecu) {
    setRaw(ecu, EcuSignal::EngineSpeed, 6000);
    setRaw(ecu, EcuSignal::LambdaAverage, 1000);
    setRaw(ecu, EcuSignal::LambdaA, 990);
    setRaw(ecu, EcuSignal::LambdaB, 1010);
    setRaw(ecu, EcuSignal::FuelInjPulsewidth, 1250);
    setRaw(ecu, EcuSignal::FuelInjDuty, 455);
    setRaw(ecu, EcuSignal::KnockDetected, 1);
    setRaw(ecu, EcuSignal::RevLimitRpm, 7000);
    setRaw(ecu, EcuSignal::LambdaTarget, -1000);
    setRaw(ecu, EcuSignal::BatteryVoltage, 1380);
    setRaw(ecu, EcuSignal::IntakeAirTemp, 253);
    setRaw(ecu, EcuSignal::CoolantTemp, 905);
    setRaw(ecu, EcuSignal::GearPosn, 3);
}

static LoadCellSample validSample(int32_t raw, uint32_t t_ms) {
    LoadCellSample s;
    s.raw = raw;
    s.timestamp_ms = t_ms;
    s.status = LoadCellStatus::Valid;
    return s;
}

static ForceMeasurement usableForce(double n, uint32_t t_ms, uint32_t cal_version = 2, uint32_t tare_version = 3) {
    ForceMeasurement f;
    f.available = true;
    f.force_n = n;
    f.timestamp_ms = t_ms;
    f.calibration_ready = true;
    f.calibration_version = cal_version;
    f.tare_version = tare_version;
    return f;
}

// Everything fresh and valid at t = 1000 ms: 6000 rpm, 100 N.
static MeasurementInputs allGood() {
    MeasurementInputs in;
    setTypicalEcu(in.ecu);
    receiveAll(in.ecu, 1000);
    in.loadcell = validSample(120000, 1000);
    in.force = usableForce(100.0, 1000);
    return in;
}

// The CSV column(s) each ECU message feeds, in mask-bit order.
static const char* const kMessageColumns[kEcuMessageCount][3] = {
    {"rpm", "lambda_avg", nullptr},                          // bit 0 0x520
    {"lambda_a", "lambda_b", nullptr},                       // bit 1 0x521
    {"fuel_inj_pulse_width_ms", "fuel_inj_duty_pct", nullptr}, // bit 2 0x522
    {"knock_detected", "rev_limit_rpm", nullptr},            // bit 3 0x526
    {"lambda_target", nullptr, nullptr},                     // bit 4 0x527
    {"battery_v", "intake_air_temp_c", "coolant_temp_c"},    // bit 5 0x530
    {"gear", nullptr, nullptr},                              // bit 6 0x536
};

// ---------------------------------------------------------------------------
// ECU columns

static void test_all_13_signals_in_engineering_units() {
    const Row r = formatRow(allGood(), 1000);
    TEST_ASSERT_EQUAL_STRING("127", r["can_received_mask"].c_str());
    TEST_ASSERT_EQUAL_STRING("127", r["can_fresh_mask"].c_str());
    TEST_ASSERT_EQUAL_STRING("6000", r["rpm"].c_str());
    TEST_ASSERT_EQUAL_STRING("1.000", r["lambda_avg"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.990", r["lambda_a"].c_str());
    TEST_ASSERT_EQUAL_STRING("1.010", r["lambda_b"].c_str());
    TEST_ASSERT_EQUAL_STRING("12.50", r["fuel_inj_pulse_width_ms"].c_str());
    TEST_ASSERT_EQUAL_STRING("45.5", r["fuel_inj_duty_pct"].c_str());
    TEST_ASSERT_EQUAL_STRING("1", r["knock_detected"].c_str());
    TEST_ASSERT_EQUAL_STRING("7000", r["rev_limit_rpm"].c_str());
    TEST_ASSERT_EQUAL_STRING("-1.000", r["lambda_target"].c_str());
    TEST_ASSERT_EQUAL_STRING("13.80", r["battery_v"].c_str());
    TEST_ASSERT_EQUAL_STRING("25.3", r["intake_air_temp_c"].c_str());
    TEST_ASSERT_EQUAL_STRING("90.5", r["coolant_temp_c"].c_str());
    TEST_ASSERT_EQUAL_STRING("3", r["gear"].c_str()); // the DBC's raw gear code, not converted
}

static void test_signal_limits_print_exactly() {
    MeasurementInputs in;
    receiveAll(in.ecu, 1000);
    for (size_t i = 0; i < kEcuSignalCount; ++i) in.ecu.raw_values[i] = 65535; // u16 max everywhere...
    setRaw(in.ecu, EcuSignal::KnockDetected, 1);
    setRaw(in.ecu, EcuSignal::LambdaTarget, -32768); // ...i16 min on the signed one
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("65535", r["rpm"].c_str()); // not negative (no int16 wrap)
    TEST_ASSERT_EQUAL_STRING("65.535", r["lambda_avg"].c_str());
    TEST_ASSERT_EQUAL_STRING("655.35", r["fuel_inj_pulse_width_ms"].c_str());
    TEST_ASSERT_EQUAL_STRING("6553.5", r["fuel_inj_duty_pct"].c_str());
    TEST_ASSERT_EQUAL_STRING("-32.768", r["lambda_target"].c_str());
    TEST_ASSERT_EQUAL_STRING("6553.5", r["coolant_temp_c"].c_str());
    TEST_ASSERT_EQUAL_STRING("65535", r["gear"].c_str());
}

static void test_small_signed_value_keeps_its_sign() {
    MeasurementInputs in = allGood();
    setRaw(in.ecu, EcuSignal::LambdaTarget, -1);
    TEST_ASSERT_EQUAL_STRING("-0.001", formatRow(in, 1000)["lambda_target"].c_str());
}

static void test_valid_zeroes_stay_numeric() {
    MeasurementInputs in = allGood();
    for (size_t i = 0; i < kEcuSignalCount; ++i) in.ecu.raw_values[i] = 0;
    in.loadcell = validSample(0, 1000);
    in.force = usableForce(0.0, 1000);
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("0", r["rpm"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.000", r["lambda_avg"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.00", r["battery_v"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.0", r["coolant_temp_c"].c_str());
    TEST_ASSERT_EQUAL_STRING("0", r["knock_detected"].c_str());
    TEST_ASSERT_EQUAL_STRING("0", r["gear"].c_str());
    TEST_ASSERT_EQUAL_STRING("0", r["loadcell_raw"].c_str());
    TEST_ASSERT_EQUAL_STRING("1", r["loadcell_valid"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.000", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.000", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.0", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.000", r["horsepower_hp"].c_str());
}

static void test_each_message_goes_stale_independently() {
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        MeasurementInputs in = allGood();
        receiveAll(in.ecu, 2500);
        in.ecu.messages[m].last_rx_ms = 1000; // age 2000 at t=3000: stale (freshness 2000)
        in.loadcell = validSample(120000, 3000);
        in.force = usableForce(100.0, 3000);
        const Row r = formatRow(in, 3000);

        const uint8_t bit = static_cast<uint8_t>(1u << m);
        TEST_ASSERT_EQUAL_STRING("127", r["can_received_mask"].c_str());
        TEST_ASSERT_EQUAL_STRING(std::to_string(127 & ~bit).c_str(), r["can_fresh_mask"].c_str());
        for (size_t other = 0; other < kEcuMessageCount; ++other) {
            for (const char* col : kMessageColumns[other]) {
                if (!col) continue;
                if (other == m) {
                    TEST_ASSERT_EQUAL_STRING_MESSAGE("", r[col].c_str(), col);
                } else {
                    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, r[col].size(), col);
                }
            }
        }
        TEST_ASSERT_EQUAL_STRING("120000", r["loadcell_raw"].c_str()); // load cell unaffected
        TEST_ASSERT_EQUAL_STRING("25.000", r["torque_nm"].c_str());
    }
}

static void test_message_fresh_until_age_reaches_limit() {
    MeasurementInputs in = allGood(); // received at 1000
    TEST_ASSERT_EQUAL_STRING("6000", formatRow(in, 2999)["rpm"].c_str()); // age 1999
    TEST_ASSERT_EQUAL_STRING("", formatRow(in, 3000)["rpm"].c_str());     // age 2000
}

static void test_never_received_message_is_blank() {
    MeasurementInputs in = allGood();
    in.ecu.messages[static_cast<size_t>(EcuMessage::Gear)] = EcuMessageStatus{};
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("63", r["can_received_mask"].c_str());
    TEST_ASSERT_EQUAL_STRING("63", r["can_fresh_mask"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["gear"].c_str());
}

static void test_freshness_survives_millis_rollover() {
    MeasurementInputs in = allGood();
    receiveAll(in.ecu, 0xFFFFFF00u);              // just before the wrap
    in.loadcell = validSample(5, 0xFFFFFF00u);
    in.force = usableForce(100.0, 0xFFFFFF00u);
    const Row r = formatRow(in, 100);             // 356 ms later, after the wrap
    TEST_ASSERT_EQUAL_STRING("100", r["timestamp_ms"].c_str());
    TEST_ASSERT_EQUAL_STRING("127", r["can_fresh_mask"].c_str());
    TEST_ASSERT_EQUAL_STRING("5", r["loadcell_raw"].c_str());
    TEST_ASSERT_EQUAL_STRING("100.000", r["force_n"].c_str());
}

// ---------------------------------------------------------------------------
// Load cell, calibration and versions

static void test_loadcell_loss_blanks_only_load_values() {
    MeasurementInputs in = allGood();
    in.loadcell.status = LoadCellStatus::Fault; // keeps the last good raw, must not be shown
    in.force.available = false;                 // #35 can't convert a faulted sample
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("", r["loadcell_raw"].c_str());
    TEST_ASSERT_EQUAL_STRING("0", r["loadcell_valid"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["horsepower_hp"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("6000", r["rpm"].c_str()); // CAN unaffected
    TEST_ASSERT_EQUAL_STRING("127", r["can_fresh_mask"].c_str());
}

static void test_loadcell_raw_fresh_until_age_reaches_limit() {
    MeasurementInputs in = allGood(); // sample at 1000, freshness 500
    TEST_ASSERT_EQUAL_STRING("120000", formatRow(in, 1499)["loadcell_raw"].c_str());
    const Row stale = formatRow(in, 1500);
    TEST_ASSERT_EQUAL_STRING("", stale["loadcell_raw"].c_str());
    TEST_ASSERT_EQUAL_STRING("0", stale["loadcell_valid"].c_str());
}

static void test_signed_loadcell_and_force() {
    MeasurementInputs in = allGood();
    in.loadcell = validSample(-8388608, 1000); // HX711 24-bit minimum
    in.force = usableForce(-100.0, 1000);
    in.ecu.raw_values[static_cast<size_t>(EcuSignal::EngineSpeed)] = 600;
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("-8388608", r["loadcell_raw"].c_str());
    TEST_ASSERT_EQUAL_STRING("-100.000", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("-25.000", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("-1570.8", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("-2.106", r["horsepower_hp"].c_str());
}

static void test_raw_shown_while_calibration_incomplete() {
    MeasurementInputs in = allGood();
    in.force = ForceMeasurement{}; // no accepted calibration yet (today's firmware, before #35)
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("120000", r["loadcell_raw"].c_str());
    TEST_ASSERT_EQUAL_STRING("1", r["loadcell_valid"].c_str());
    TEST_ASSERT_EQUAL_STRING("0", r["calibration_ready"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["tare_version"].c_str());
}

static void test_collecting_tare_blanks_force_and_versions() {
    MeasurementInputs in = allGood();
    in.force.calibration_ready = false; // tare/calibration collecting
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("0", r["calibration_ready"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["tare_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("120000", r["loadcell_raw"].c_str());
}

static void test_versions_are_the_forces_own_pair() {
    MeasurementInputs in = allGood();
    in.force = usableForce(100.0, 1000, 7, 9);
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("1", r["calibration_ready"].c_str());
    TEST_ASSERT_EQUAL_STRING("7", r["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("9", r["tare_version"].c_str());
}

// A row taken before a tare, formatted (queued) after it, keeps the old pair.
static void test_snapshot_taken_before_tare_keeps_old_versions() {
    MeasurementInputs in = allGood();
    in.force = usableForce(100.0, 1000, 2, 3);
    const DynoSnapshot before = takeSnapshot(in, fixtureConfig(), 1000, 0);

    in.force = usableForce(0.0, 1100, 2, 4); // tare accepted: new offset, tare_version 3 -> 4
    const DynoSnapshot after = takeSnapshot(in, fixtureConfig(), 1100, 0);

    OutputLine line;
    TEST_ASSERT_TRUE(formatCsvRow(before, line));
    const Row old_row{splitCells(line)};
    TEST_ASSERT_EQUAL_STRING("100.000", old_row["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("2", old_row["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("3", old_row["tare_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("1000", old_row["timestamp_ms"].c_str());

    TEST_ASSERT_TRUE(formatCsvRow(after, line));
    const Row new_row{splitCells(line)};
    TEST_ASSERT_EQUAL_STRING("0.000", new_row["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("4", new_row["tare_version"].c_str());
}

static void test_stale_force_blanks_force_and_versions() {
    MeasurementInputs in = allGood(); // force at 1000, freshness 500
    in.loadcell = validSample(120000, 1500);
    const Row r = formatRow(in, 1500);
    TEST_ASSERT_EQUAL_STRING("", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("1", r["calibration_ready"].c_str()); // readiness is as copied
}

// ---------------------------------------------------------------------------
// Torque and power

static void test_reference_torque_and_power() {
    MeasurementInputs in = allGood();
    in.ecu.raw_values[static_cast<size_t>(EcuSignal::EngineSpeed)] = 600;
    const Row r = formatRow(in, 1000); // 100 N, 0.25 m, 600 rpm (issue #37)
    TEST_ASSERT_EQUAL_STRING("100.000", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("25.000", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("1570.8", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("2.106", r["horsepower_hp"].c_str());
}

static void test_missing_rpm_blanks_only_power() {
    MeasurementInputs in = allGood();
    in.ecu.messages[static_cast<size_t>(EcuMessage::Engine1)] = EcuMessageStatus{}; // 0x520 never arrived
    const Row r = formatRow(in, 1000);
    TEST_ASSERT_EQUAL_STRING("", r["rpm"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["horsepower_hp"].c_str());
    TEST_ASSERT_EQUAL_STRING("25.000", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("100.000", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("2", r["calibration_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("0.990", r["lambda_a"].c_str());
    TEST_ASSERT_EQUAL_STRING("126", r["can_fresh_mask"].c_str());
}

static void test_unconfirmed_geometry_blanks_torque_and_power_only() {
    SnapshotConfig real_like = fixtureConfig();
    real_like.setup = {0.0, false, false}; // today's DynoConfig.h: nothing confirmed
    const Row r = formatRow(allGood(), 1000, 0, real_like);
    TEST_ASSERT_EQUAL_STRING("100.000", r["force_n"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["torque_nm"].c_str());
    TEST_ASSERT_EQUAL_STRING("", r["power_w"].c_str());
    TEST_ASSERT_EQUAL_STRING("2", r["calibration_version"].c_str());
}

// Every #37 example row (valid and invalid) lands in the right cells.
static void test_torque_power_examples_dataset() {
    for (const TorquePowerExample& ex : kTorquePowerExamples) {
        MeasurementInputs in;
        // Feed the example's RPM through the ECU state, as the firmware does.
        if (ex.speed.received && ex.speed.rpm >= 0 && ex.speed.rpm <= 65535 && ex.speed.rpm == floor(ex.speed.rpm)) {
            in.ecu.messages[static_cast<size_t>(EcuMessage::Engine1)] = {true, ex.speed.timestamp_ms};
            in.ecu.raw_values[static_cast<size_t>(EcuSignal::EngineSpeed)] = static_cast<int32_t>(ex.speed.rpm);
        } else if (ex.speed.received) {
            continue; // NaN/negative/fractional RPM can't come from the u16 CAN signal
        }
        in.force = ex.force;
        SnapshotConfig config = fixtureConfig();
        config.setup = ex.setup;
        const Row r = formatRow(in, ex.now_ms, 0, config);

        char expected[32];
        if (ex.torque_valid) {
            snprintf(expected, sizeof(expected), "%.3f", ex.torque_nm);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, r["torque_nm"].c_str(), ex.name);
        } else {
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r["torque_nm"].c_str(), ex.name);
        }
        if (ex.power_valid) {
            snprintf(expected, sizeof(expected), "%.1f", ex.power_w);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, r["power_w"].c_str(), ex.name);
            snprintf(expected, sizeof(expected), "%.3f", ex.horsepower_hp);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, r["horsepower_hp"].c_str(), ex.name);
        } else {
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r["power_w"].c_str(), ex.name);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r["horsepower_hp"].c_str(), ex.name);
        }
        if (ex.versions_known) {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(std::to_string(ex.calibration_version).c_str(),
                                             r["calibration_version"].c_str(), ex.name);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(std::to_string(ex.tare_version).c_str(), r["tare_version"].c_str(),
                                             ex.name);
        } else {
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r["calibration_version"].c_str(), ex.name);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", r["force_n"].c_str(), ex.name);
        }
    }
}

// ---------------------------------------------------------------------------
// Fixed columns and limits

static void test_dropped_rows_and_timestamp_printed() {
    const Row r = formatRow(MeasurementInputs{}, 4294967295u, 12345);
    TEST_ASSERT_EQUAL_STRING("1", r["schema_version"].c_str());
    TEST_ASSERT_EQUAL_STRING("4294967295", r["timestamp_ms"].c_str());
    TEST_ASSERT_EQUAL_STRING("12345", r["output_dropped_rows"].c_str());
}

static void test_overflow_gives_no_row_never_a_truncated_one() {
    const DynoSnapshot snap = takeSnapshot(allGood(), fixtureConfig(), 1000, 0);
    OutputLine line;
    TEST_ASSERT_FALSE(formatCsvRow(snap, line, 60));
    TEST_ASSERT_EQUAL_UINT(0, line.length);
    TEST_ASSERT_EQUAL_STRING("", line.text);
}

// The widest row any real input can produce still fits an OutputLine, with
// room to spare. (Force is bounded far above any load cell we could fit.)
static void test_widest_row_fits() {
    MeasurementInputs in;
    receiveAll(in.ecu, 4000000000u);
    for (size_t i = 0; i < kEcuSignalCount; ++i) in.ecu.raw_values[i] = 65535;
    setRaw(in.ecu, EcuSignal::LambdaTarget, -32768);
    in.loadcell = validSample(-8388608, 4000000000u);
    in.force = usableForce(-1.0e9, 4000000000u, 4294967295u, 4294967295u);
    SnapshotConfig config = fixtureConfig();
    config.setup = {2.0, true, true};
    const DynoSnapshot snap = takeSnapshot(in, config, 4000000000u, 4294967295u);
    OutputLine line;
    TEST_ASSERT_TRUE(formatCsvRow(snap, line));
    TEST_ASSERT_LESS_THAN_UINT(OutputLine::kCapacity - 64, line.length);
    const Row r{splitCells(line)};
    TEST_ASSERT_NOT_EQUAL(0, r["power_w"].size()); // the big numbers really were printed
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_header_matches_issue_with_crlf);
    RUN_TEST(test_startup_row_is_blank_except_fixed_columns);
    RUN_TEST(test_all_13_signals_in_engineering_units);
    RUN_TEST(test_signal_limits_print_exactly);
    RUN_TEST(test_small_signed_value_keeps_its_sign);
    RUN_TEST(test_valid_zeroes_stay_numeric);
    RUN_TEST(test_each_message_goes_stale_independently);
    RUN_TEST(test_message_fresh_until_age_reaches_limit);
    RUN_TEST(test_never_received_message_is_blank);
    RUN_TEST(test_freshness_survives_millis_rollover);
    RUN_TEST(test_loadcell_loss_blanks_only_load_values);
    RUN_TEST(test_loadcell_raw_fresh_until_age_reaches_limit);
    RUN_TEST(test_signed_loadcell_and_force);
    RUN_TEST(test_raw_shown_while_calibration_incomplete);
    RUN_TEST(test_collecting_tare_blanks_force_and_versions);
    RUN_TEST(test_versions_are_the_forces_own_pair);
    RUN_TEST(test_snapshot_taken_before_tare_keeps_old_versions);
    RUN_TEST(test_stale_force_blanks_force_and_versions);
    RUN_TEST(test_reference_torque_and_power);
    RUN_TEST(test_missing_rpm_blanks_only_power);
    RUN_TEST(test_unconfirmed_geometry_blanks_torque_and_power_only);
    RUN_TEST(test_torque_power_examples_dataset);
    RUN_TEST(test_dropped_rows_and_timestamp_printed);
    RUN_TEST(test_overflow_gives_no_row_never_a_truncated_one);
    RUN_TEST(test_widest_row_fits);
    return UNITY_END();
}
