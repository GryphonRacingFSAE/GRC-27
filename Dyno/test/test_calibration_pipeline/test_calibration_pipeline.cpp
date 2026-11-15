// Calibration end to end (#35 step 5): a scripted 10 Hz load cell through the
// real LoadCellAcquirer and LoadCellCalibrator (stepped every 5 ms, like
// LoadCellTask), into real CSV rows (takeSnapshot + formatCsvRow) every
// 100 ms. Checks what the CSV shows before, during and after a calibration
// and a tare, and that a row made before the tare keeps its old versions
// even when printed after it. The tare button (#36) must give the same CSV
// as typing "tare".
#include <stdlib.h>
#include <unity.h>

#include <map>
#include <string>
#include <vector>

#include "CalibrationExamples.h"
#include "CommandParser.h"
#include "CsvRow.h"
#include "CsvSchema.h"
#include "FakeButton.h"
#include "FakeCalibrationStore.h"
#include "FakeCommands.h"
#include "FakeLoadCellProvider.h"
#include "LoadCellAcquirer.h"
#include "LoadCellCalibrator.h"
#include "TareButton.h"
#include "TareButtonExamples.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

// What the rig does (10 Hz):        what the operator types:
//   0 -  3 s  nothing hung: 100000    1.0 s  cal unloaded  (stable ~2.0 s)
//   3 -  6 s  100 N hung:   120000    4.0 s  cal loaded 100N (stable ~5.0 s)
//   6 -  9 s  50 N:         110000
//   9 - 12 s  zero drifted: 101000   10.0 s  tare          (stable ~11.0 s)
//  12 - 15 s  50 N again:   111000
static const FakePhase kPhases[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},  {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 9000, FakeMode::Ready, 110000, 0}, {9000, 12000, FakeMode::Ready, 101000, 0},
    {12000, 15000, FakeMode::Ready, 111000, 0},
};
static const FakeLoadCellScript kScript = {kPhases, sizeof kPhases / sizeof kPhases[0], 100000, 0, 0};

static SnapshotConfig fixtureConfig() {
    SnapshotConfig c;
    c.can_freshness_ms = 2000;
    c.loadcell_freshness_ms = 500;
    c.setup = {0.25, true, true}; // FIXTURE ONLY geometry
    c.limits = kExampleLimits;
    return c;
}

// A printed row's cells by column name.
static std::map<std::string, std::string> cellsOf(const OutputLine& line) {
    std::string body(line.text, line.length);
    TEST_ASSERT_TRUE(body.size() >= 2 && body.compare(body.size() - 2, 2, "\r\n") == 0);
    body.resize(body.size() - 2);
    std::map<std::string, std::string> cells;
    size_t start = 0;
    for (size_t i = 0; i < kCsvColumnCount; ++i) {
        const size_t comma = body.find(',', start);
        cells[kCsvColumns[i].name] = body.substr(start, comma - start);
        start = comma + 1;
    }
    return cells;
}

struct Rig {
    FakeLoadCellProvider cell{kScript};
    LoadCellAcquirer acq{cell, LoadCellAcquirerConfig{150, 3, 1000}};
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal{store, replies, kExampleCalibratorConfig};
    ForceMeasurement force; // the "mailbox"
    std::map<uint32_t, std::map<std::string, std::string>> rows; // by time made
    std::map<uint32_t, std::string> printed;                     // the same rows, as bytes

    // The tare button (#36): active-low, 50 ms debounce, polled every 20 ms,
    // held down while button_from <= t < button_to.
    FakeButton pin;
    FakeCommandSink queue; // the load-cell command queue
    TareButton button{pin, queue, replies, kExampleTareButtonConfig};
    uint32_t button_from = UINT32_MAX, button_to = UINT32_MAX;

    // Runs from `from` to before `to`: a 5 ms task loop, a row every 100 ms.
    void run(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t < to; t += 5) {
            if (t % 20 == 0) {
                pin.high = !(t >= button_from && t < button_to);
                button.step(t);
            }
            for (const LoadCellCommand& c : queue.queued) cal.command(c, t);
            queue.queued.clear();
            cell.setTime(t);
            acq.step(t);
            force = cal.step(acq.sample(), t);
            if (t % 100 == 0) {
                const OutputLine line = row(t);
                rows[t] = cellsOf(line);
                printed[t] = std::string(line.text, line.length);
            }
        }
    }

    OutputLine row(uint32_t t) const {
        MeasurementInputs in;
        in.loadcell = acq.sample();
        in.force = force;
        OutputLine line;
        TEST_ASSERT_TRUE(formatCsvRow(takeSnapshot(in, fixtureConfig(), t, 0), line));
        return line;
    }

    void type(const char* text, uint32_t t) {
        const ParseResult p = parseCommand(text);
        TEST_ASSERT_EQUAL_STRING("None", parseErrorName(p.error));
        cal.command(p.command, t);
    }
};

static void assertBlank(const std::map<std::string, std::string>& r, uint32_t t) {
    char msg[32];
    snprintf(msg, sizeof msg, "row at %lu ms", static_cast<unsigned long>(t));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0", r.at("calibration_ready").c_str(), msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.at("force_n").c_str(), msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.at("calibration_version").c_str(), msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.at("tare_version").c_str(), msg);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", r.at("torque_nm").c_str(), msg);
}

static void assertForce(const std::map<std::string, std::string>& r, const char* force, const char* cal_v,
                        const char* tare_v) {
    TEST_ASSERT_EQUAL_STRING("1", r.at("calibration_ready").c_str());
    TEST_ASSERT_EQUAL_STRING(force, r.at("force_n").c_str());
    TEST_ASSERT_EQUAL_STRING(cal_v, r.at("calibration_version").c_str());
    TEST_ASSERT_EQUAL_STRING(tare_v, r.at("tare_version").c_str());
}

static void test_calibration_and_tare_as_the_csv_shows_them() {
    Rig rig;
    rig.cal.begin();

    rig.run(0, 1000);
    rig.type("cal unloaded", 1000);
    rig.run(1000, 4000);
    rig.type("cal loaded 100N", 4000);
    rig.run(4000, 9900);
    const OutputLine queued = rig.row(9900); // made, not yet printed
    rig.run(9900, 10000);
    rig.type("tare", 10000);
    rig.run(10000, 15000);

    // Uncalibrated, then calibrating: blank force and versions throughout,
    // while the raw counts still print.
    for (uint32_t t = 0; t < 5000; t += 100) assertBlank(rig.rows.at(t), t);
    TEST_ASSERT_EQUAL_STRING("100000", rig.rows.at(500).at("loadcell_raw").c_str());
    // Stable exactly one window (1 s) after "cal loaded": the 5.0 s row
    // already reads the 100 N reference.
    assertForce(rig.rows.at(5000), "100.000", "1", "1");

    // (110000 - 100000) / 200 = 50 N under calibration 1/1.
    for (uint32_t t = 6200; t < 9000; t += 100) assertForce(rig.rows.at(t), "50.000", "1", "1");
    // The drifted zero shows as 5 N until the tare: (101000 - 100000) / 200.
    assertForce(rig.rows.at(9500), "5.000", "1", "1");

    // Taring: blank again.
    for (uint32_t t = 10000; t < 11000; t += 100) assertBlank(rig.rows.at(t), t);

    // After the tare: (111000 - 101000) / 200 = 50 N, tare_version 2.
    for (uint32_t t = 12200; t < 15000; t += 100) assertForce(rig.rows.at(t), "50.000", "1", "2");

    // The row made at 9.9 s, printed now, still says 5 N under 1/1.
    const std::map<std::string, std::string> old_row = cellsOf(queued);
    assertForce(old_row, "5.000", "1", "1");
    TEST_ASSERT_EQUAL_STRING("9900", old_row.at("timestamp_ms").c_str());

    // One write (the calibration), and the operator saw each step.
    TEST_ASSERT_EQUAL_UINT32(1, rig.store.writes);
    bool cal_ok = false, tare_ok = false;
    for (const std::string& l : rig.replies.lines) {
        cal_ok |= l.rfind("cal OK", 0) == 0;
        tare_ok |= l.rfind("tare OK", 0) == 0;
    }
    TEST_ASSERT_TRUE(cal_ok);
    TEST_ASSERT_TRUE(tare_ok);
}

// The same session with the tare done by the button instead of typing it.
// Pressed at 9.94 s, the press is accepted at the 10.00 s poll (60 ms held,
// first poll >= 50 ms), so the Tare reaches the calibrator at 10.0 s, the
// moment "tare" was typed above. Every CSV row, 0 to 15 s, must then be the
// same bytes: the button is just another way to send the same command.
static void test_the_button_gives_the_same_csv_as_typing_tare() {
    Rig typed;
    typed.cal.begin();
    typed.run(0, 1000);
    typed.type("cal unloaded", 1000);
    typed.run(1000, 4000);
    typed.type("cal loaded 100N", 4000);
    typed.run(4000, 10000);
    typed.type("tare", 10000);
    typed.run(10000, 15000);

    Rig pressed;
    pressed.button_from = 9940;
    pressed.button_to = 10400;
    pressed.cal.begin();
    pressed.run(0, 1000);
    pressed.type("cal unloaded", 1000);
    pressed.run(1000, 4000);
    pressed.type("cal loaded 100N", 4000);
    pressed.run(4000, 15000);

    TEST_ASSERT_EQUAL_UINT32(150, typed.printed.size());
    TEST_ASSERT_EQUAL_UINT32(150, pressed.printed.size());
    for (uint32_t t = 0; t < 15000; t += 100) {
        char msg[32];
        snprintf(msg, sizeof msg, "row at %lu ms", static_cast<unsigned long>(t));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(typed.printed.at(t).c_str(), pressed.printed.at(t).c_str(), msg);
    }
    // ...and it really was the button that tared (not a no-op on both sides).
    TEST_ASSERT_EQUAL_UINT32(1, pressed.button.presses());
    assertForce(pressed.rows.at(12500), "50.000", "1", "2");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_calibration_and_tare_as_the_csv_shows_them);
    RUN_TEST(test_the_button_gives_the_same_csv_as_typing_tare);
    return UNITY_END();
}
