// Whole fake pipeline (#38): the DYNO_FAKE_INPUTS demo scripts through the
// real CanReceiver + LoadCellAcquirer (polled every 5 ms like the tasks),
// a row every 100 ms into an 8-line queue, and a serial link that is slow,
// then jammed, then fine again. Proves: bounded memory, every printed row is
// complete, every missing row is counted, and acquisition never stalls.
#include <stdlib.h>
#include <unity.h>

#include <string>
#include <vector>

#include "CanReceiver.h"
#include "CsvPublisher.h"
#include "CsvSchema.h"
#include "FakeCanBus.h"
#include "FakeLoadCellProvider.h"
#include "LineQueue.h"
#include "LoadCellAcquirer.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

static const uint32_t kRowIntervalMs = 100;
static const uint32_t kPollMs = 5;

static SnapshotConfig fixtureConfig() {
    SnapshotConfig c;
    c.can_freshness_ms = 2000;
    c.loadcell_freshness_ms = 500;
    c.setup = {0.25, true, true}; // FIXTURE ONLY geometry
    c.limits = kExampleLimits;
    return c;
}

struct PrintedRow {
    std::vector<std::string> cells;
    uint32_t timestamp() const { return static_cast<uint32_t>(strtoul(cells[1].c_str(), nullptr, 10)); }
    uint32_t dropped() const { return static_cast<uint32_t>(strtoul(cells.back().c_str(), nullptr, 10)); }
    const std::string& cell(const char* name) const {
        for (size_t i = 0; i < kCsvColumnCount; ++i) {
            if (std::string(kCsvColumns[i].name) == name) return cells[i];
        }
        TEST_FAIL_MESSAGE(name);
        return cells[0];
    }
};

// One CRLF-terminated line -> cells. Fails on a partial / malformed line.
static std::vector<std::string> splitCells(const std::string& text) {
    TEST_ASSERT_TRUE_MESSAGE(text.size() >= 2 && text.compare(text.size() - 2, 2, "\r\n") == 0, text.c_str());
    const std::string body = text.substr(0, text.size() - 2);
    TEST_ASSERT_EQUAL_MESSAGE(std::string::npos, body.find_first_of("\r\n"), text.c_str());
    std::vector<std::string> cells;
    size_t start = 0;
    for (;;) {
        const size_t comma = body.find(',', start);
        cells.push_back(body.substr(start, comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return cells;
}

struct Pipeline {
    FakeCanBus bus{demoEcuScript()};
    CanReceiver rx{bus, CanReceiverConfig{20, 1000}};
    FakeLoadCellProvider cell{demoLoadCellScript()};
    LoadCellAcquirer acq{cell, LoadCellAcquirerConfig{150, 3, 1000}};
    BoundedLineQueue<8> queue;
    CsvPublisher pub{queue, fixtureConfig()};

    std::string header;
    std::vector<PrintedRow> rows;
    uint32_t rows_made = 0;

    // The acquisition tasks' work for one 5 ms tick.
    void acquire(uint32_t t) {
        bus.setTime(t);
        for (int guard = 0; guard < 1000 && rx.step(t) != CanStepResult::Idle; ++guard) {
        }
        cell.setTime(t);
        acq.step(t);
    }

    // The serial writer: print one queued line, if any.
    void printOne() {
        OutputLine line;
        if (!queue.tryPop(line)) return;
        const std::string text(line.text, line.length);
        TEST_ASSERT_NOT_EQUAL_MESSAGE('#', text[0], text.c_str()); // no diagnostics expected here
        const std::vector<std::string> cells = splitCells(text);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(kCsvColumnCount, cells.size(), text.c_str());
        if (cells[0] == "schema_version") {
            TEST_ASSERT_TRUE_MESSAGE(header.empty() && rows.empty(), "header twice or after rows");
            header = text;
            return;
        }
        TEST_ASSERT_FALSE_MESSAGE(header.empty(), "row before header");
        TEST_ASSERT_EQUAL_STRING("1", cells[0].c_str());
        rows.push_back(PrintedRow{cells});
    }

    // Run [from, to): acquisition every 5 ms, a row every 100 ms, and the
    // serial link prints one line every print_every_ms (0 = jammed).
    void run(uint32_t from, uint32_t to, uint32_t print_every_ms) {
        for (uint32_t t = from; t != to; t += kPollMs) {
            acquire(t);
            if (t % kRowIntervalMs == 0) {
                pub.publishRow(MeasurementInputs{rx.ecu(), acq.sample(), ForceMeasurement{}}, t);
                ++rows_made;
            }
            if (print_every_ms != 0 && t % print_every_ms == 0) printOne();
            TEST_ASSERT_TRUE(queue.size() <= 8);
        }
    }

    void drainAll() {
        while (queue.size() > 0) printOne();
    }

    const PrintedRow* rowAt(uint32_t timestamp) const {
        for (const PrintedRow& r : rows) {
            if (r.timestamp() == timestamp) return &r;
        }
        return nullptr;
    }
};

// Every printed row's gap from the previous one is explained exactly by the
// rise in output_dropped_rows; and printed + dropped == made.
static void checkEveryMissingRowIsCounted(const Pipeline& p) {
    TEST_ASSERT_FALSE(p.rows.empty());
    TEST_ASSERT_EQUAL_UINT32(0, p.rows.front().timestamp());
    for (size_t i = 1; i < p.rows.size(); ++i) {
        const PrintedRow& a = p.rows[i - 1];
        const PrintedRow& b = p.rows[i];
        TEST_ASSERT_TRUE(b.timestamp() > a.timestamp());
        const uint32_t missing = (b.timestamp() - a.timestamp()) / kRowIntervalMs - 1;
        char msg[64];
        snprintf(msg, sizeof(msg), "between %u and %u ms", static_cast<unsigned>(a.timestamp()),
                 static_cast<unsigned>(b.timestamp()));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(missing, b.dropped() - a.dropped(), msg);
    }
    TEST_ASSERT_EQUAL_UINT32(p.rows_made, p.rows.size() + p.pub.counters().dropped_rows);
    TEST_ASSERT_EQUAL_UINT32(0, p.pub.counters().row_overflows);
}

// ---------------------------------------------------------------------------

// A link fast enough for every row: nothing dropped, 10 rows/s exactly.
static void test_fast_link_prints_every_row() {
    Pipeline p;
    p.pub.start();
    p.run(0, 30000, kPollMs);
    p.drainAll();
    TEST_ASSERT_EQUAL_UINT32(300, p.rows.size());
    TEST_ASSERT_EQUAL_UINT32(0, p.pub.counters().dropped_rows);
    checkEveryMissingRowIsCounted(p);
}

// Slow (a line every 250 ms), then jammed for 10 s, then fine: the queue
// stays at 8 lines, the drops are all counted, rows stay whole.
static void test_slow_then_jammed_link_drops_whole_rows_and_counts_them() {
    Pipeline p;
    p.pub.start();
    p.run(0, 5000, 250);       // 2.5x too slow
    p.run(5000, 15000, 0);     // jammed (e.g. host stopped reading)
    p.run(15000, 25000, kPollMs);
    p.drainAll();

    TEST_ASSERT_TRUE(p.pub.counters().dropped_rows > 100);
    checkEveryMissingRowIsCounted(p);
    // Once the link recovers, every row is printed again.
    for (uint32_t t = 16000; t < 25000; t += kRowIntervalMs) {
        TEST_ASSERT_NOT_NULL(p.rowAt(t));
    }
}

// Acquisition carries on while output is jammed: the rows printed after the
// jam show the inputs as they are THEN, not as they were before it.
static void test_acquisition_keeps_running_while_output_is_jammed() {
    Pipeline p;
    p.pub.start();
    p.run(0, 3000, kPollMs);
    p.run(3000, 12000, 0); // jammed across the demo's "0x530 stops" at 10 s
    p.run(12000, 12500, kPollMs);
    p.drainAll();

    const PrintedRow* r = p.rowAt(12100);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("127", r->cell("can_received_mask").c_str());
    TEST_ASSERT_EQUAL_STRING("95", r->cell("can_fresh_mask").c_str()); // 0x530 stale since 11980
    TEST_ASSERT_EQUAL_STRING("7050", r->cell("rpm").c_str());          // demo ramp 1000 + t/2
    TEST_ASSERT_EQUAL_STRING("", r->cell("intake_air_temp_c").c_str()); // 0x530 signal blank when stale
    TEST_ASSERT_EQUAL_STRING("1", r->cell("loadcell_valid").c_str());
    const long raw = strtol(r->cell("loadcell_raw").c_str(), nullptr, 10);
    TEST_ASSERT_TRUE_MESSAGE(raw > 12000 && raw <= 12100, r->cell("loadcell_raw").c_str()); // 1000 counts/s ramp
}

// The demo's timeline, as the CSV shows it (link fast enough throughout).
static void test_demo_timeline_in_the_csv() {
    Pipeline p;
    p.pub.start();
    p.run(0, 30000, kPollMs);
    p.drainAll();

    struct Expect {
        uint32_t t;
        const char* received;
        const char* fresh;
        const char* loadcell_valid;
    };
    const Expect expects[] = {
        {0, "0", "0", "0"},          // nothing yet
        {1900, "0", "0", "1"},       // ECU silent at startup; load cell running
        {2100, "127", "127", "1"},   // all ECU messages
        {12000, "127", "95", "1"},   // 0x530 stale
        {17000, "127", "0", "1"},    // ECU silent: all stale, still "received"
        {21000, "127", "127", "1"},  // ECU back
        {28000, "127", "127", "0"},  // load cell "unplugged" 27-30 s
    };
    for (const Expect& e : expects) {
        const PrintedRow* r = p.rowAt(e.t);
        char msg[32];
        snprintf(msg, sizeof(msg), "at %u ms", static_cast<unsigned>(e.t));
        TEST_ASSERT_NOT_NULL_MESSAGE(r, msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.received, r->cell("can_received_mask").c_str(), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.fresh, r->cell("can_fresh_mask").c_str(), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(e.loadcell_valid, r->cell("loadcell_valid").c_str(), msg);
        // No calibration (#35) in firmware yet: force and everything after it blank.
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", r->cell("force_n").c_str(), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", r->cell("torque_nm").c_str(), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", r->cell("power_w").c_str(), msg);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("0", r->cell("calibration_ready").c_str(), msg);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fast_link_prints_every_row);
    RUN_TEST(test_slow_then_jammed_link_drops_whole_rows_and_counts_them);
    RUN_TEST(test_acquisition_keeps_running_while_output_is_jammed);
    RUN_TEST(test_demo_timeline_in_the_csv);
    return UNITY_END();
}
