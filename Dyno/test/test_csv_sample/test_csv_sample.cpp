// docs/csv-sample.csv (#38) is hand-written from the DBC scales and #37's
// worked example. This checks the formatter prints exactly those lines, so the
// published sample can't drift from the firmware.
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "CsvPublisher.h"
#include "CsvSchema.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

static std::string samplePath() {
    const std::string self = __FILE__;
    const std::string suffix = "test/test_csv_sample/test_csv_sample.cpp";
    return self.substr(0, self.size() - suffix.size()) + "docs/csv-sample.csv";
}

// Lines of the sample, each still with its "\r\n".
static std::vector<std::string> sampleLines() {
    std::ifstream f(samplePath(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), samplePath().c_str());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line + "\n");
    return lines;
}

static SnapshotConfig fixtureConfig() {
    SnapshotConfig c;
    c.can_freshness_ms = 2000;
    c.loadcell_freshness_ms = 500;
    c.setup = {0.25, true, true}; // FIXTURE ONLY geometry
    c.limits = kExampleLimits;
    return c;
}

static void setRaw(EcuState& e, EcuSignal s, int32_t raw) { e.raw_values[static_cast<size_t>(s)] = raw; }

static void receive(EcuState& e, EcuMessage m, uint32_t t) {
    e.messages[static_cast<size_t>(m)].received = true;
    e.messages[static_cast<size_t>(m)].last_rx_ms = t;
}

static EcuState runningEcu(uint32_t rx_ms) {
    EcuState e;
    setRaw(e, EcuSignal::EngineSpeed, 6000);
    setRaw(e, EcuSignal::LambdaAverage, 1000);
    setRaw(e, EcuSignal::LambdaA, 990);
    setRaw(e, EcuSignal::LambdaB, 1010);
    setRaw(e, EcuSignal::FuelInjPulsewidth, 1250);
    setRaw(e, EcuSignal::FuelInjDuty, 455);
    setRaw(e, EcuSignal::KnockDetected, 0);
    setRaw(e, EcuSignal::RevLimitRpm, 11000);
    setRaw(e, EcuSignal::LambdaTarget, 950);
    setRaw(e, EcuSignal::BatteryVoltage, 1380);
    setRaw(e, EcuSignal::IntakeAirTemp, 253);
    setRaw(e, EcuSignal::CoolantTemp, 905);
    setRaw(e, EcuSignal::GearPosn, 3);
    for (size_t m = 0; m < kEcuMessageCount; ++m) receive(e, static_cast<EcuMessage>(m), rx_ms);
    return e;
}

static LoadCellSample cell(int32_t raw, uint32_t t, LoadCellStatus status = LoadCellStatus::Valid) {
    LoadCellSample s;
    s.raw = raw;
    s.timestamp_ms = t;
    s.status = status;
    return s;
}

struct SampleRow {
    MeasurementInputs in;
    uint32_t t;
    uint32_t dropped;
};

// The situations the sample shows, in file order.
static std::vector<SampleRow> sampleRows() {
    std::vector<SampleRow> rows;
    rows.push_back({MeasurementInputs{}, 0, 0}); // startup: nothing yet

    MeasurementInputs ecu_silent;
    ecu_silent.loadcell = cell(120000, 1490);
    rows.push_back({ecu_silent, 1500, 0});

    MeasurementInputs running;
    running.ecu = runningEcu(2480);
    running.loadcell = cell(120000, 2490);
    rows.push_back({running, 2500, 0});

    MeasurementInputs with_force = running; // FIXTURE: what #35 will provide
    with_force.ecu = runningEcu(2580);
    with_force.loadcell = cell(120000, 2590);
    with_force.force.available = true;
    with_force.force.force_n = 100.0;
    with_force.force.timestamp_ms = 2590;
    with_force.force.calibration_ready = true;
    with_force.force.calibration_version = 2;
    with_force.force.tare_version = 3;
    rows.push_back({with_force, 2600, 0});

    MeasurementInputs no_530;
    no_530.ecu = runningEcu(11980);
    receive(no_530.ecu, EcuMessage::Environment, 9980); // 0x530 stopped
    no_530.loadcell = cell(120000, 11990);
    rows.push_back({no_530, 12000, 0});

    MeasurementInputs cell_fault;
    cell_fault.ecu = runningEcu(27980);
    cell_fault.loadcell = cell(120000, 26990, LoadCellStatus::Fault);
    rows.push_back({cell_fault, 28000, 0});

    MeasurementInputs after_drops = running;
    after_drops.ecu = runningEcu(29980);
    after_drops.loadcell = cell(120000, 29990);
    rows.push_back({after_drops, 30000, 3});
    return rows;
}

// ---------------------------------------------------------------------------

static void test_sample_header_is_the_schema_header() {
    OutputLine header;
    TEST_ASSERT_TRUE(formatCsvHeader(header));
    TEST_ASSERT_EQUAL_STRING(header.text, sampleLines().at(0).c_str());
}

static void test_sample_rows_are_what_the_formatter_prints() {
    const std::vector<std::string> lines = sampleLines();
    const std::vector<SampleRow> rows = sampleRows();
    size_t next = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].compare(0, 2, "# ") == 0) continue;
        TEST_ASSERT_TRUE_MESSAGE(next < rows.size(), "sample has more rows than the test describes");
        const SampleRow& r = rows[next++];
        OutputLine line;
        TEST_ASSERT_TRUE(formatCsvRow(takeSnapshot(r.in, fixtureConfig(), r.t, r.dropped), line));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(lines[i].c_str(), line.text, ("sample line " + std::to_string(i + 1)).c_str());
    }
    TEST_ASSERT_EQUAL_UINT(rows.size(), next);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sample_header_is_the_schema_header);
    RUN_TEST(test_sample_rows_are_what_the_formatter_prints);
    return UNITY_END();
}
