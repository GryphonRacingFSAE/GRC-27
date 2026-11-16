// Whole-dyno integration on the host (#39 step 2). SYNTHETIC: every input is
// a scripted fake and the clock is a test clock.
//
// One "rig" runs every real piece the firmware runs, joined the way
// src/Tasks/TaskFactory.cpp joins them:
//
//   FakeCanBus -> CanReceiver ------------------------------> ECU mailbox --+
//   FakeLoadCellProvider -> LoadCellAcquirer -> sample mailbox -------------+
//         typed lines -> parseCommand --+                                   |
//   FakeButton -> TareButton -----------+-> command queue                   |
//                                             |                             v
//                   LoadCellCalibrator <------+   -> force mailbox --> CsvPublisher
//                                                                      (takeSnapshot,
//                                                                       #37 calc, CSV)
//                                                                           |
//                                     "PC" reading at a chosen speed <- output queue
//
// What this covers: the pieces fit together (types, versions, freshness,
// independence, bounded output). What it can't: the FreeRTOS task files
// themselves (they only move data between these pieces; the rig copies
// that wiring here). Those are checked on a devkit (docs/bench-test.md).
//
// One test per row of the #39 failure matrix; see docs/bench-test.md.
#include <stdlib.h>
#include <unity.h>

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "CalibrationExamples.h"
#include "CanReceiver.h"
#include "CommandParser.h"
#include "CsvPublisher.h"
#include "CsvSchema.h"
#include "FakeButton.h"
#include "FakeCalibrationStore.h"
#include "FakeCanBus.h"
#include "FakeCommands.h"
#include "FakeLoadCellProvider.h"
#include "LineQueue.h"
#include "LoadCellAcquirer.h"
#include "LoadCellCalibrator.h"
#include "TareButton.h"
#include "TareButtonExamples.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Settings: the same numbers as DynoConfig.h today, written out so the
// hand-worked timings below don't silently change meaning on a retune.

static const uint32_t kTickMs = 5;          // HX711_POLL_INTERVAL_MS
static const uint32_t kCommandPollMs = 20;  // COMMAND_POLL_INTERVAL_MS
static const uint32_t kRowMs = 100;         // CSV_ROW_INTERVAL_MS
static const size_t kOutputQueueLines = 16; // CSV_OUTPUT_QUEUE_LINES

static SnapshotConfig fixtureConfig() {
    SnapshotConfig c;
    c.can_freshness_ms = 2000;
    c.loadcell_freshness_ms = 500;
    c.setup = {0.25, true, true}; // FIXTURE ONLY geometry: 0.25 m arm, same shaft
    c.limits = kExampleLimits;
    return c;
}

// ---------------------------------------------------------------------------
// Scripts

// All 7 ECU messages at 50 Hz for the whole run.
static const FakeEcuPhase kEcuAll[] = {{0, 600000, 0x7F}};
static const FakeEcuScript kEcuAllScript = {kEcuAll, 1, 20, 0, 0};

// The load cell (10 Hz) for a calibration session:
//   0-3 s  nothing hung   100000      "cal unloaded" at 1 s (settles ~2 s)
//   3-6 s  100 N hung     120000      "cal loaded 100N" at 4 s (settles ~5 s)
//   6 s on 50 N           110000      -> (110000 - 100000) / 200 = 50 N
static const FakePhase kCellSession[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},
    {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 600000, FakeMode::Ready, 110000, 0},
};
static const FakeLoadCellScript kCellSessionScript = {kCellSession, 3, 100000, 0, 0};

// ---------------------------------------------------------------------------
// The rig

// Calibrator/button replies: kept for the test, and sent as "# " lines into
// the same output queue as the rows, like printDiagnostic() in the firmware.
class RigReplies : public CalibrationReplies {
  public:
    explicit RigReplies(LineQueue& queue) : queue_(queue) {}
    FakeReplies kept;
    void reply(const char* text) override {
        kept.reply(text);
        OutputLine line;
        formatDiagnostic(line, "%s", text);
        queue_.tryPush(line); // never waits; a full queue loses the line
    }

  private:
    LineQueue& queue_;
};

using Row = std::map<std::string, std::string>;

static Row cellsOf(const std::string& text) {
    TEST_ASSERT_TRUE_MESSAGE(text.size() >= 2 && text.compare(text.size() - 2, 2, "\r\n") == 0, text.c_str());
    const std::string body = text.substr(0, text.size() - 2);
    std::vector<std::string> cells;
    size_t start = 0;
    for (;;) {
        const size_t comma = body.find(',', start);
        cells.push_back(body.substr(start, comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(kCsvColumnCount, cells.size(), text.c_str());
    Row row;
    for (size_t i = 0; i < kCsvColumnCount; ++i) row[kCsvColumns[i].name] = cells[i];
    return row;
}

struct Rig {
    // CanTask
    FakeCanBus bus;
    CanReceiver rx{bus, CanReceiverConfig{20, 1000}};
    // LoadCellTask
    FakeLoadCellProvider cell;
    LoadCellAcquirer acq{cell, LoadCellAcquirerConfig{150, 3, 1000}};
    FakeCalibrationStore& store; // outlives the rig: a "reboot" is a new rig on the same store
    // The output queue + its writer (OutputTask)
    BoundedLineQueue<kOutputQueueLines> out;
    RigReplies replies{out};
    LoadCellCalibrator cal{store, replies, kExampleCalibratorConfig};
    // CommandTask
    FakeButton pin;
    FakeCommandSink commands; // the 4-slot command queue
    TareButton button{pin, commands, replies, kExampleTareButtonConfig};
    // CsvTask
    CsvPublisher pub{out, fixtureConfig()};
    bool started = false;

    // Mailboxes, pre-filled like TaskFactory: nothing yet.
    EcuState ecu_box;
    LoadCellSample sample_box;
    ForceMeasurement force_box;

    // The "PC": reads one line every print_every_ms (0 = stopped reading).
    uint32_t print_every_ms = kTickMs;
    std::vector<std::string> printed;

    std::map<uint32_t, std::string> typed;                   // what the operator types, by time
    std::vector<std::pair<uint32_t, uint32_t>> button_downs; // [from, to): button held
    uint32_t rows_made = 0;

    Rig(const FakeEcuScript& ecu, const FakeLoadCellScript& load, FakeCalibrationStore& s)
        : bus(ecu), cell(load), store(s) {
        cal.begin(); // LoadCellTask: load the saved calibration first
    }

    void tick(uint32_t t) {
        // CanTask: drain everything due, publish each change.
        bus.setTime(t);
        for (int guard = 0; guard < 1000; ++guard) {
            const CanStepResult r = rx.step(t);
            if (r == CanStepResult::Decoded) ecu_box = rx.ecu();
            if (r == CanStepResult::Idle) break;
        }
        // CommandTask: button, then typed lines, into the command queue.
        if (t % kCommandPollMs == 0) {
            bool down = false;
            for (const auto& d : button_downs) down |= (t >= d.first && t < d.second);
            pin.high = !down; // active-low
            button.step(t);
        }
        const auto line = typed.find(t);
        if (line != typed.end()) {
            const ParseResult p = parseCommand(line->second.c_str());
            TEST_ASSERT_EQUAL_STRING_MESSAGE("None", parseErrorName(p.error), line->second.c_str());
            commands.send(p.command);
        }
        // LoadCellTask
        cell.setTime(t);
        if (acq.step(t)) sample_box = acq.sample();
        while (!commands.queued.empty()) {
            cal.command(commands.queued.front(), t);
            commands.queued.pop_front();
        }
        force_box = cal.step(acq.sample(), t);
        // CsvTask
        if (t % kRowMs == 0) {
            if (!started) started = pub.start();
            if (started) {
                pub.publishRow(MeasurementInputs{ecu_box, sample_box, force_box}, t);
                ++rows_made;
            }
        }
        // OutputTask
        if (print_every_ms != 0 && t % print_every_ms == 0) printOne();
        TEST_ASSERT_TRUE(out.size() <= kOutputQueueLines);
    }

    // [from, to)
    void run(uint32_t from, uint32_t to) {
        for (uint32_t t = from; t < to; t += kTickMs) tick(t);
    }

    void printOne() {
        OutputLine line;
        if (out.tryPop(line)) printed.emplace_back(line.text, line.length);
    }
    void drain() {
        while (out.size() > 0) printOne();
    }

    // The operator's usual start: calibrate with the 100 N reference.
    void calibrateAtOneAndFourSeconds() {
        typed[1000] = "cal unloaded";
        typed[4000] = "cal loaded 100N";
    }

    // Printed data rows (no header, no "# " lines), in print order.
    std::vector<Row> rows() const {
        std::vector<Row> r;
        for (const std::string& l : printed) {
            if (l[0] == '#' || l.rfind("schema_version", 0) == 0) continue;
            r.push_back(cellsOf(l));
        }
        return r;
    }
    Row rowAt(uint32_t timestamp) const {
        for (const Row& r : rows()) {
            if (r.at("timestamp_ms") == std::to_string(timestamp)) return r;
        }
        char msg[48];
        snprintf(msg, sizeof msg, "no row printed for %lu ms", static_cast<unsigned long>(timestamp));
        TEST_FAIL_MESSAGE(msg);
        return Row{};
    }
};

static void assertCell(const Row& r, const char* column, const char* expected) {
    char msg[96];
    snprintf(msg, sizeof msg, "%s at %s ms", column, r.at("timestamp_ms").c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, r.at(column).c_str(), msg);
}

// ---------------------------------------------------------------------------
// Matrix row 1: all approved CAN inputs and a valid calibrated load.

// Every column of one row, against values worked out by hand:
//   rpm: fake ECU ramp 1000 + t/2 at t = 12 s  -> 7000
//   force: (110000 - 100000) / 200 counts/N    -> 50.000 N
//   torque: 50 N x 0.25 m (fixture arm)        -> 12.500 N.m
//   power: 12.5 x 7000 x 2pi/60                -> 9163.0 W
//   hp: 9162.98 / 745.699872                   -> 12.288
static void test_all_inputs_valid_and_calibrated_gives_the_worked_row() {
    FakeCalibrationStore store;
    Rig rig(kEcuAllScript, kCellSessionScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.run(0, 13000);
    rig.drain();

    const Row r = rig.rowAt(12000);
    const char* expected[][2] = {
        {"schema_version", "1"},
        {"timestamp_ms", "12000"},
        {"can_received_mask", "127"},
        {"can_fresh_mask", "127"},
        {"rpm", "7000"},
        {"lambda_avg", "1.000"},
        {"lambda_a", "0.990"},
        {"lambda_b", "1.010"},
        {"fuel_inj_pulse_width_ms", "11.00"},
        {"fuel_inj_duty_pct", "45.0"},
        {"knock_detected", "0"},
        {"rev_limit_rpm", "11000"},
        {"lambda_target", "0.950"},
        {"battery_v", "13.26"},
        {"intake_air_temp_c", "34.6"},
        {"coolant_temp_c", "88.5"},
        {"gear", "1"},
        {"loadcell_raw", "110000"},
        {"force_n", "50.000"},
        {"torque_nm", "12.500"},
        {"power_w", "9163.0"},
        {"horsepower_hp", "12.288"},
        {"loadcell_valid", "1"},
        {"calibration_ready", "1"},
        {"calibration_version", "1"},
        {"tare_version", "1"},
        {"output_dropped_rows", "0"},
    };
    TEST_ASSERT_EQUAL_UINT(kCsvColumnCount, sizeof expected / sizeof expected[0]);
    for (const auto& e : expected) assertCell(r, e[0], e[1]);
}

// ---------------------------------------------------------------------------
// Matrix row 2: some ECU messages never arrive.

// 0x530 (Environment, bit 5) and 0x536 (Gear, bit 6) never sent: mask 0x1F.
static const FakeEcuPhase kEcuNoEnvNoGear[] = {{0, 600000, 0x1F}};
static const FakeEcuScript kEcuNoEnvNoGearScript = {kEcuNoEnvNoGear, 1, 20, 0, 0};

static void test_missing_ecu_messages_stay_blank_while_the_rest_update() {
    FakeCalibrationStore store;
    Rig rig(kEcuNoEnvNoGearScript, kCellSessionScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.run(0, 14000);
    rig.drain();

    for (uint32_t t : {12000u, 13000u}) {
        const Row r = rig.rowAt(t);
        assertCell(r, "can_received_mask", "31"); // bits 0-4 only, ever
        assertCell(r, "can_fresh_mask", "31");
        for (const char* never : {"battery_v", "intake_air_temp_c", "coolant_temp_c", "gear"}) {
            assertCell(r, never, ""); // unavailable, not 0
        }
        assertCell(r, "lambda_avg", "1.000");
        assertCell(r, "force_n", "50.000");
        assertCell(r, "torque_nm", "12.500");
    }
    // RPM keeps updating, and power with it (engine speed is in 0x520).
    assertCell(rig.rowAt(12000), "rpm", "7000");
    assertCell(rig.rowAt(13000), "rpm", "7500");
    assertCell(rig.rowAt(12000), "power_w", "9163.0");
    assertCell(rig.rowAt(13000), "power_w", "9817.5"); // 12.5 x 7500 x 2pi/60 = 9817.48
}

// Every 100 ms slot from `from` to `to` has exactly one printed row: the
// pipeline never stalled waiting for a source.
static void assertNoGaps(const Rig& rig, uint32_t from, uint32_t to) {
    std::map<std::string, int> seen;
    for (const Row& r : rig.rows()) ++seen[r.at("timestamp_ms")];
    for (uint32_t t = from; t < to; t += kRowMs) {
        char msg[48];
        snprintf(msg, sizeof msg, "rows printed for %lu ms", static_cast<unsigned long>(t));
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen[std::to_string(t)], msg);
    }
}

// ---------------------------------------------------------------------------
// Matrix row 3: the ECU/CAN stops; the load cell carries on.

// All messages until 8 s, silence 8-12 s, all again from 12 s.
static const FakeEcuPhase kEcuStops[] = {{0, 8000, 0x7F}, {12000, 600000, 0x7F}};
static const FakeEcuScript kEcuStopsScript = {kEcuStops, 2, 20, 0, 0};

// The calibration session, but the hung load changes to 60 N at 10 s while
// the ECU is silent, so "the load cell carries on" is visible.
static const FakePhase kCellChangesAtTen[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},
    {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 10000, FakeMode::Ready, 110000, 0},
    {10000, 600000, FakeMode::Ready, 112000, 0}, // (112000 - 100000) / 200 = 60 N
};
static const FakeLoadCellScript kCellChangesAtTenScript = {kCellChangesAtTen, 4, 100000, 0, 0};

static void test_ecu_stops_load_cell_carries_on_and_power_goes_invalid() {
    FakeCalibrationStore store;
    Rig rig(kEcuStopsScript, kCellChangesAtTenScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.run(0, 14000);
    rig.drain();

    // Last frames at 7980 ms (rpm 1000 + 7980/2 = 4990). Power needs force
    // and RPM at most 250 ms apart (#37), so it goes first: valid at 8.2 s
    // (220 ms apart), blank from 8.3 s (320 ms) although the RPM cell still
    // shows a fresh value. A shared row is not proof the readings match.
    assertCell(rig.rowAt(8200), "power_w", "6531.9"); // 12.5 x 4990 x 2pi/60 = 6531.89
    const Row skewed = rig.rowAt(8300);
    assertCell(skewed, "rpm", "4990");
    assertCell(skewed, "power_w", "");
    assertCell(skewed, "torque_nm", "12.500");
    // 9.9 s: 1920 ms old, still fresh (limit 2000).
    const Row before = rig.rowAt(9900);
    assertCell(before, "can_fresh_mask", "127");
    assertCell(before, "rpm", "4990");
    assertCell(before, "power_w", "");
    // 10.0 s: 2020 ms old: stale. Still "received", but every ECU cell blank.
    const Row stale = rig.rowAt(10000);
    assertCell(stale, "can_received_mask", "127");
    assertCell(stale, "can_fresh_mask", "0");
    assertCell(stale, "rpm", "");
    assertCell(stale, "coolant_temp_c", "");
    assertCell(stale, "power_w", "");
    assertCell(stale, "horsepower_hp", "");
    // The load cell carried on: the new 60 N load shows, torque from it is valid.
    const Row later = rig.rowAt(11000);
    assertCell(later, "loadcell_raw", "112000");
    assertCell(later, "force_n", "60.000");
    assertCell(later, "torque_nm", "15.000");
    assertCell(later, "power_w", "");
    // ECU back at 12 s: power returns with no operator action.
    const Row back = rig.rowAt(13000);
    assertCell(back, "can_fresh_mask", "127");
    assertCell(back, "rpm", "7500");
    assertCell(back, "power_w", "11781.0"); // 15 x 7500 x 2pi/60 = 11780.97

    assertNoGaps(rig, 0, 14000);
}

// ---------------------------------------------------------------------------
// Matrix row 4: the HX711 stops responding; CAN carries on.

// The calibration session, then "unplugged" 8-12 s, then back.
static const FakePhase kCellUnplugged[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},
    {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 8000, FakeMode::Ready, 110000, 0},
    {8000, 12000, FakeMode::NeverReady, 0, 0},
    {12000, 600000, FakeMode::Ready, 110000, 0},
};
static const FakeLoadCellScript kCellUnpluggedScript = {kCellUnplugged, 5, 100000, 0, 0};

static void test_hx711_stops_can_carries_on_and_nothing_waits() {
    FakeCalibrationStore store;
    Rig rig(kEcuAllScript, kCellUnpluggedScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.run(0, 16000);
    rig.drain();

    assertCell(rig.rowAt(7500), "force_n", "50.000");
    for (uint32_t t : {9000u, 10000u, 11000u}) {
        const Row r = rig.rowAt(t);
        assertCell(r, "loadcell_valid", "0");
        assertCell(r, "loadcell_raw", ""); // unavailable, not the last value or 0
        assertCell(r, "force_n", "");
        assertCell(r, "torque_nm", "");
        assertCell(r, "power_w", "");
        assertCell(r, "calibration_version", "");
        assertCell(r, "can_fresh_mask", "127");
    }
    // CAN carried on.
    assertCell(rig.rowAt(9000), "rpm", "5500");
    assertCell(rig.rowAt(11000), "rpm", "6500");
    // Plugged back in at 12 s: recovered, same calibration, no operator action.
    const Row back = rig.rowAt(15000);
    assertCell(back, "force_n", "50.000");
    assertCell(back, "calibration_version", "1");
    assertCell(back, "power_w", "11126.5"); // 12.5 x 8500 x 2pi/60 = 11126.47
    TEST_ASSERT_TRUE(rig.acq.counters().faults_entered >= 1);
    TEST_ASSERT_TRUE(rig.acq.counters().recoveries >= 1);

    // No wait longer than the CAN receive timeout, no row skipped.
    TEST_ASSERT_TRUE(rig.bus.longestTimeoutMs() <= 20);
    assertNoGaps(rig, 0, 16000);
}

// ---------------------------------------------------------------------------
// Matrix row 5: a tare while older rows are still waiting to be printed.

// The calibration session, then the zero drifts to 101000 at 9 s (shows as
// 5 N until a tare), and 50 N is hung again from 13 s.
static const FakePhase kCellDrift[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},
    {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 9000, FakeMode::Ready, 110000, 0},
    {9000, 13000, FakeMode::Ready, 101000, 0},
    {13000, 600000, FakeMode::Ready, 111000, 0},
};
static const FakeLoadCellScript kCellDriftScript = {kCellDrift, 5, 100000, 0, 0};

static long cellLong(const Row& r, const char* column) {
    return strtol(r.at(column).c_str(), nullptr, 10);
}

static void test_tare_never_mixes_a_queued_reading_with_the_new_zero() {
    FakeCalibrationStore store;
    Rig rig(kEcuAllScript, kCellDriftScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.typed[10000] = "tare";
    rig.run(0, 9000);
    rig.print_every_ms = 400; // the PC falls behind: rows pile up in the queue
    rig.run(9000, 11500);

    // The tare has finished, yet rows made before it are still queued.
    TEST_ASSERT_EQUAL_UINT32(2, rig.cal.active().tare_version);
    const size_t printed_before = rig.printed.size();
    rig.drain();
    bool queued_old_row = false;
    for (size_t i = printed_before; i < rig.printed.size(); ++i) {
        const std::string& l = rig.printed[i];
        if (l[0] != '#') queued_old_row |= cellsOf(l).at("tare_version") == "1";
    }
    TEST_ASSERT_TRUE_MESSAGE(queued_old_row, "no tare-1 row was still queued after the tare");
    rig.print_every_ms = kTickMs;
    bool old_after_new = false;
    rig.run(11500, 15000);
    rig.drain();

    // Every printed row's force is its own raw reading under its own
    // versions: (raw - 100000)/200 for tare 1, (raw - 101000)/200 for tare 2.
    // Never the new zero applied to an old reading, or the reverse.
    size_t v1 = 0, v2 = 0;
    for (const Row& r : rig.rows()) {
        const std::string& tare = r.at("tare_version");
        if (tare.empty()) {
            assertCell(r, "force_n", "");
            continue;
        }
        assertCell(r, "calibration_version", "1");
        const long zero = tare == "1" ? 100000 : (tare == "2" ? 101000 : -1);
        TEST_ASSERT_TRUE_MESSAGE(zero > 0, tare.c_str());
        char expected[32];
        snprintf(expected, sizeof expected, "%.3f", (cellLong(r, "loadcell_raw") - zero) / 200.0);
        assertCell(r, "force_n", expected);
        (tare == "1" ? v1 : v2)++;
        old_after_new |= (tare == "1" && v2 > 0);
    }
    TEST_ASSERT_TRUE(v1 > 0 && v2 > 0);
    TEST_ASSERT_FALSE_MESSAGE(old_after_new, "a tare-1 row printed after a tare-2 row");
    TEST_ASSERT_TRUE(rig.pub.counters().dropped_rows > 0); // the queue really did fill
    // The drifted zero read 5 N before the tare, and reads a valid 0 N after.
    assertCell(rig.rowAt(9500), "force_n", "5.000");
    assertCell(rig.rowAt(12500), "force_n", "0.000");
    assertCell(rig.rowAt(14000), "force_n", "50.000");
}

// ---------------------------------------------------------------------------
// Matrix row 6: button bounce, a repeated request, and an unstable tare.

// As kCellDrift, but from 13 s the reading creeps (+2000 counts/s), so a
// tare then can never settle (spread limit 500 counts per 1 s window).
static const FakePhase kCellCreeps[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},
    {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 9000, FakeMode::Ready, 110000, 0},
    {9000, 13000, FakeMode::Ready, 101000, 0},
    {13000, 600000, FakeMode::Ready, 111000, 2000},
};
static const FakeLoadCellScript kCellCreepsScript = {kCellCreeps, 5, 100000, 0, 0};

static void test_bounce_repeat_and_unstable_tare_leave_one_clean_tare() {
    FakeCalibrationStore store;
    Rig rig(kEcuAllScript, kCellCreepsScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.button_downs = {
        {9500, 9520},   // contact bounce: one 20 ms blip, shorter than the 50 ms debounce
        {9940, 9960},   // a bouncy press...
        {9980, 10400},  // ...that then holds: ONE press, accepted at the 10.04 s poll
        {10600, 10800}, // pressed again while that tare is still collecting: refused
        {14000, 14400}, // a tare while the reading creeps: can't settle
    };
    rig.run(0, 26000);
    rig.drain();

    TEST_ASSERT_EQUAL_UINT32(3, rig.button.presses());
    TEST_ASSERT_TRUE(rig.replies.kept.any("tare: busy"));
    TEST_ASSERT_EQUAL_UINT(1, rig.replies.kept.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT(1, rig.replies.kept.count("tare FAILED: reading not stable"));
    // One tare happened (version 1 -> 2), the failed one changed nothing,
    // and only the calibration itself was ever written to storage.
    const CalibrationRecord active = rig.cal.active();
    TEST_ASSERT_EQUAL_UINT32(1, active.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, active.tare_version);
    TEST_ASSERT_EQUAL_UINT32(1, store.writes);

    assertCell(rig.rowAt(12500), "force_n", "0.000");
    assertCell(rig.rowAt(12500), "tare_version", "2");
    // During the failed attempt: blank (collecting). After it: the kept zero.
    assertCell(rig.rowAt(15000), "calibration_ready", "0");
    assertCell(rig.rowAt(15000), "force_n", "");
    const Row after = rig.rowAt(25000);
    assertCell(after, "calibration_ready", "1");
    assertCell(after, "tare_version", "2");
    char expected[32];
    snprintf(expected, sizeof expected, "%.3f", (cellLong(after, "loadcell_raw") - 101000) / 200.0);
    assertCell(after, "force_n", expected);
    // CAN carried on throughout.
    assertCell(after, "can_fresh_mask", "127");
}

// ---------------------------------------------------------------------------
// Matrix row 7: a real zero stays valid, and signs are not clamped away.

// The calibration session, then nothing hung (0 N) 6-9 s, then pulled the
// other way (-50 N) from 9 s.
static const FakePhase kCellZeroThenNegative[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},
    {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 9000, FakeMode::Ready, 100000, 0},
    {9000, 600000, FakeMode::Ready, 90000, 0}, // (90000 - 100000) / 200 = -50 N
};
static const FakeLoadCellScript kCellZeroThenNegativeScript = {kCellZeroThenNegative, 4, 100000, 0, 0};

static void test_zero_is_a_valid_value_and_negative_keeps_its_sign() {
    FakeCalibrationStore store;
    Rig rig(kEcuAllScript, kCellZeroThenNegativeScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.run(0, 12000);
    rig.drain();

    // Zero: printed as 0, not blank (a measured zero is not "missing").
    const Row zero = rig.rowAt(8000);
    assertCell(zero, "force_n", "0.000");
    assertCell(zero, "torque_nm", "0.000");
    assertCell(zero, "power_w", "0.0");
    assertCell(zero, "horsepower_hp", "0.000");
    // Negative: -50 N x 0.25 m = -12.5 N.m; x 6500 rpm x 2pi/60 = -8508.5 W.
    const Row negative = rig.rowAt(11000);
    assertCell(negative, "force_n", "-50.000");
    assertCell(negative, "torque_nm", "-12.500");
    assertCell(negative, "power_w", "-8508.5");
    assertCell(negative, "horsepower_hp", "-11.410");
}

// ---------------------------------------------------------------------------
// Matrix row 8: a slow, then stalled, serial sink with a full queue.

static void test_stalled_pc_drops_counted_rows_while_everything_else_runs() {
    FakeCalibrationStore store;
    Rig rig(kEcuAllScript, kCellChangesAtTenScript, store);
    rig.calibrateAtOneAndFourSeconds();
    rig.typed[12000] = "cal status"; // typed while the output is stuck
    rig.run(0, 6000);
    rig.print_every_ms = 250; // 2.5x too slow for 10 rows/s
    rig.run(6000, 8000);
    rig.print_every_ms = 0; // the PC stops reading for 10 s
    const uint32_t samples_before = rig.acq.counters().samples;
    rig.run(8000, 18000);
    const uint32_t samples_during = rig.acq.counters().samples - samples_before;
    rig.print_every_ms = kTickMs;
    rig.run(18000, 20000);
    rig.drain();

    // Acquisition and control never waited for the PC: ~10 Hz of samples
    // during the stall, and the typed command was handled.
    TEST_ASSERT_UINT32_WITHIN(2, 100, samples_during);
    TEST_ASSERT_TRUE(rig.replies.kept.any("cal status: calibrated"));
    // Rows after the stall show the inputs as they are now.
    const Row now = rig.rowAt(18500);
    assertCell(now, "rpm", "10250"); // 1000 + 18500/2
    assertCell(now, "force_n", "60.000");

    // Bounded and observable: every missing row is counted, every printed
    // row is whole (cellsOf checks 27 cells + CRLF), and made = printed + dropped.
    const std::vector<Row> rows = rig.rows();
    TEST_ASSERT_TRUE(rig.pub.counters().dropped_rows > 50);
    TEST_ASSERT_EQUAL_UINT32(rig.rows_made, rows.size() + rig.pub.counters().dropped_rows);
    for (size_t i = 1; i < rows.size(); ++i) {
        const long gap = (cellLong(rows[i], "timestamp_ms") - cellLong(rows[i - 1], "timestamp_ms")) / kRowMs - 1;
        const long counted =
            cellLong(rows[i], "output_dropped_rows") - cellLong(rows[i - 1], "output_dropped_rows");
        TEST_ASSERT_EQUAL_INT_MESSAGE(gap, counted, rows[i].at("timestamp_ms").c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(0, rig.pub.counters().row_overflows);
}

// ---------------------------------------------------------------------------
// Matrix row 9: restart / reset.

// After the reboot nothing is hung, but the zero is still the drifted 101000.
static const FakePhase kCellDriftedFromBoot[] = {{0, 600000, FakeMode::Ready, 101000, 0}};
static const FakeLoadCellScript kCellDriftedFromBootScript = {kCellDriftedFromBoot, 1, 100000, 0, 0};

// The first line that isn't a "# " diagnostic.
static std::string firstNonDiagnostic(const Rig& rig) {
    for (const std::string& l : rig.printed) {
        if (l[0] != '#') return l;
    }
    return "";
}

static void test_restart_restores_the_calibration_but_not_the_tare() {
    FakeCalibrationStore store; // the ESP32's flash: survives the reboot

    // A board that was never calibrated: says so, force blank.
    {
        Rig fresh(kEcuAllScript, kCellDriftScript, store);
        TEST_ASSERT_TRUE(fresh.replies.kept.any("WARNING: no calibration stored"));
        fresh.run(0, 1000);
        fresh.drain();
        assertCell(fresh.rowAt(500), "calibration_ready", "0");
        assertCell(fresh.rowAt(500), "force_n", "");
    }

    // Boot 1: calibrate, then tare the drifted zero (versions 1/2).
    {
        Rig boot1(kEcuAllScript, kCellDriftScript, store);
        boot1.calibrateAtOneAndFourSeconds();
        boot1.typed[10000] = "tare";
        boot1.run(0, 13000);
        boot1.drain();
        assertCell(boot1.rowAt(12500), "force_n", "0.000");
        assertCell(boot1.rowAt(12500), "tare_version", "2");
    }

    // Boot 2: a new rig (every task restarted, clock from 0) on the same flash.
    Rig boot2(kEcuAllScript, kCellDriftedFromBootScript, store);
    boot2.run(0, 3000);
    boot2.drain();

    // Startup: says what it loaded; the header comes before any row.
    TEST_ASSERT_TRUE(boot2.replies.kept.any("cal: loaded the saved calibration"));
    const std::string first = firstNonDiagnostic(boot2);
    TEST_ASSERT_TRUE_MESSAGE(first.rfind("schema_version,timestamp_ms,", 0) == 0, first.c_str());
    // The calibration came back (version 1, ready from the first reading);
    // the tare did not (documented: tare again after every boot), so the
    // drifted zero reads 5 N again under tare version 1.
    const Row r = boot2.rowAt(1000);
    assertCell(r, "calibration_ready", "1");
    assertCell(r, "calibration_version", "1");
    assertCell(r, "tare_version", "1");
    assertCell(r, "force_n", "5.000"); // (101000 - 100000) / 200
    assertCell(r, "output_dropped_rows", "0");
    TEST_ASSERT_EQUAL_UINT32(1, store.writes); // only boot 1's calibration; booting wrote nothing
}

// ---------------------------------------------------------------------------
// The published synthetic run: docs/synthetic-run.csv is exactly what this
// session prints. It is the reproducible fake-input evidence for #39 step 2,
// and tools/test_check_csv.py runs the independent checker over it.
//
// To regenerate after an intended change:
//   DYNO_WRITE_SYNTHETIC_RUN=1 pio test -e native -f test_integration
// then review the diff of docs/synthetic-run.csv before committing it.

// ECU: all 0-14 s, silent 14-17 s, back from 17 s.
static const FakeEcuPhase kDemoEcu[] = {{0, 14000, 0x7F}, {17000, 600000, 0x7F}};
static const FakeEcuScript kDemoEcuScript = {kDemoEcu, 2, 20, 0, 0};
// Load cell: calibrate (0-6 s), 50 N, the zero drifts, 50 N again, unplugged 18-20 s.
static const FakePhase kDemoCell[] = {
    {0, 3000, FakeMode::Ready, 100000, 0},   {3000, 6000, FakeMode::Ready, 120000, 0},
    {6000, 9000, FakeMode::Ready, 110000, 0}, {9000, 13000, FakeMode::Ready, 101000, 0},
    {13000, 18000, FakeMode::Ready, 111000, 0}, {18000, 20000, FakeMode::NeverReady, 0, 0},
    {20000, 600000, FakeMode::Ready, 111000, 0},
};
static const FakeLoadCellScript kDemoCellScript = {kDemoCell, 7, 100000, 0, 0};

static const char* const kSyntheticBanner =
    "# SYNTHETIC RUN: host test test_integration, scripted fake CAN + load cell, test clock, "
    "FIXTURE ONLY geometry (0.25 m arm, same shaft). Not a measurement.\r\n";

static std::string syntheticRunPath() {
    const std::string self = __FILE__;
    const std::string suffix = "test/test_integration/test_integration.cpp";
    return self.substr(0, self.size() - suffix.size()) + "docs/synthetic-run.csv";
}

static std::string syntheticRun() {
    std::string text = kSyntheticBanner;
    text += "# SYNTHETIC: boot 1. 1 s cal unloaded, 4 s cal loaded 100N, 10 s tare button, ECU silent 14-17 s, "
            "load cell unplugged 18-20 s, PC stops reading 21-23 s\r\n";
    FakeCalibrationStore store;
    {
        Rig boot1(kDemoEcuScript, kDemoCellScript, store);
        boot1.calibrateAtOneAndFourSeconds();
        boot1.button_downs = {{9980, 10200}}; // accepted at the 10.04 s poll
        boot1.run(0, 21000);
        boot1.print_every_ms = 0;
        boot1.run(21000, 23000);
        boot1.print_every_ms = kTickMs;
        boot1.run(23000, 25000);
        boot1.drain();
        for (const std::string& l : boot1.printed) text += l;
    }
    text += "# SYNTHETIC: reboot (same flash); the zero is still drifted, nothing hung\r\n";
    Rig boot2(kDemoEcuScript, kCellDriftedFromBootScript, store);
    boot2.run(0, 3000);
    boot2.drain();
    for (const std::string& l : boot2.printed) text += l;
    return text;
}

static void test_synthetic_run_matches_the_published_capture() {
    const std::string run = syntheticRun();
    if (getenv("DYNO_WRITE_SYNTHETIC_RUN") != nullptr) {
        std::ofstream f(syntheticRunPath(), std::ios::binary);
        f << run;
        TEST_ASSERT_TRUE_MESSAGE(f.good(), syntheticRunPath().c_str());
    }
    std::ifstream f(syntheticRunPath(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), syntheticRunPath().c_str());
    const std::string published((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // Same bytes, or the first line that differs.
    if (published != run) {
        size_t i = 0;
        while (i < run.size() && i < published.size() && run[i] == published[i]) ++i;
        const size_t line_start = run.rfind('\n', i == 0 ? 0 : i - 1);
        const size_t from = line_start == std::string::npos ? 0 : line_start + 1;
        const std::string now = run.substr(from, run.find('\n', from) - from);
        TEST_FAIL_MESSAGE(("docs/synthetic-run.csv differs; first different line now prints: " + now).c_str());
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_synthetic_run_matches_the_published_capture);
    RUN_TEST(test_restart_restores_the_calibration_but_not_the_tare);
    RUN_TEST(test_stalled_pc_drops_counted_rows_while_everything_else_runs);
    RUN_TEST(test_zero_is_a_valid_value_and_negative_keeps_its_sign);
    RUN_TEST(test_bounce_repeat_and_unstable_tare_leave_one_clean_tare);
    RUN_TEST(test_tare_never_mixes_a_queued_reading_with_the_new_zero);
    RUN_TEST(test_ecu_stops_load_cell_carries_on_and_power_goes_invalid);
    RUN_TEST(test_hx711_stops_can_carries_on_and_nothing_waits);
    RUN_TEST(test_all_inputs_valid_and_calibrated_gives_the_worked_row);
    RUN_TEST(test_missing_ecu_messages_stay_blank_while_the_rest_update);
    return UNITY_END();
}
