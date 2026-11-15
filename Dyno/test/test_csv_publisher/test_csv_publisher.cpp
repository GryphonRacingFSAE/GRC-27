// CsvPublisher (#38): header once, rows into a bounded queue, drops counted,
// "# " diagnostics as whole lines between rows.
#include <string.h>
#include <unity.h>

#include <string>

#include "CsvPublisher.h"
#include "LineQueue.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

static SnapshotConfig fixtureConfig() {
    SnapshotConfig c;
    c.can_freshness_ms = 2000;
    c.loadcell_freshness_ms = 500;
    c.setup = {0.25, true, true}; // FIXTURE ONLY geometry (#37's example)
    c.limits = kExampleLimits;
    return c;
}

static std::string pop(BoundedLineQueue<8>& q) {
    OutputLine line;
    TEST_ASSERT_TRUE_MESSAGE(q.tryPop(line), "queue unexpectedly empty");
    return std::string(line.text, line.length);
}

static std::string lastCell(const std::string& row) {
    const size_t comma = row.rfind(',');
    return row.substr(comma + 1, row.size() - comma - 3); // without CRLF
}

static bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

// ---------------------------------------------------------------------------

static void test_start_queues_the_header_first_and_once() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    TEST_ASSERT_TRUE(pub.start());
    TEST_ASSERT_FALSE(pub.start()); // a second start does nothing
    pub.publishRow(MeasurementInputs{}, 0);

    TEST_ASSERT_TRUE(startsWith(pop(q), "schema_version,timestamp_ms,"));
    TEST_ASSERT_TRUE(startsWith(pop(q), "1,0,"));
    TEST_ASSERT_EQUAL_UINT(0, q.size());
}

static void test_rows_need_no_inputs() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    pub.start();
    for (uint32_t t = 0; t < 500; t += 100) {
        pub.publishRow(MeasurementInputs{}, t); // CAN silent, no load cell: rows still come
    }
    pop(q);
    for (uint32_t t = 0; t < 500; t += 100) {
        const std::string row = pop(q);
        TEST_ASSERT_TRUE_MESSAGE(startsWith(row, ("1," + std::to_string(t) + ",0,0,").c_str()), row.c_str());
    }
}

static void test_full_queue_drops_new_rows_and_counts_them() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    pub.start(); // 1 slot used by the header
    for (uint32_t i = 0; i < 10; ++i) {
        pub.publishRow(MeasurementInputs{}, i * 100); // nobody drains: 7 fit, 3 dropped
    }
    TEST_ASSERT_EQUAL_UINT32(3, pub.counters().dropped_rows);
    TEST_ASSERT_EQUAL_UINT(8, q.size());

    // The kept rows are the OLDEST ones (new rows are the ones discarded),
    // each complete, carrying the drop count as it was when it was made.
    pop(q); // header
    for (uint32_t i = 0; i < 7; ++i) {
        const std::string row = pop(q);
        TEST_ASSERT_TRUE(startsWith(row, ("1," + std::to_string(i * 100) + ",").c_str()));
        TEST_ASSERT_EQUAL_STRING("0", lastCell(row).c_str());
    }

    // Space again: the next row reports the cumulative drops.
    pub.publishRow(MeasurementInputs{}, 1000);
    TEST_ASSERT_EQUAL_STRING("3", lastCell(pop(q)).c_str());
}

static void test_diagnostic_is_a_whole_hash_line() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    pub.start();
    pub.publishRow(MeasurementInputs{}, 0);
    TEST_ASSERT_TRUE(pub.diagnostic("tare accepted: tare_version=%u", 4u));
    pub.publishRow(MeasurementInputs{}, 100);

    pop(q);
    TEST_ASSERT_TRUE(startsWith(pop(q), "1,0,"));
    TEST_ASSERT_EQUAL_STRING("# tare accepted: tare_version=4\r\n", pop(q).c_str());
    TEST_ASSERT_TRUE(startsWith(pop(q), "1,100,"));
}

static void test_dropped_diagnostics_are_counted_separately() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    pub.start();
    for (int i = 0; i < 7; ++i) pub.publishRow(MeasurementInputs{}, 0);
    TEST_ASSERT_FALSE(pub.diagnostic("lost"));
    TEST_ASSERT_EQUAL_UINT32(1, pub.counters().dropped_diagnostics);
    TEST_ASSERT_EQUAL_UINT32(0, pub.counters().dropped_rows); // rows only (issue #38)
}

static void test_too_long_diagnostic_is_replaced_not_cut() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    std::string huge(OutputLine::kCapacity, 'x');
    TEST_ASSERT_TRUE(pub.diagnostic("%s", huge.c_str()));
    TEST_ASSERT_EQUAL_STRING("# ERROR: diagnostic line too long, not printed\r\n", pop(q).c_str());
}

// "# " + text + CRLF + NUL must fit 512 bytes: 507 characters of text is the most.
static void test_longest_diagnostic_that_fits() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    const std::string fits(OutputLine::kCapacity - 5, 'x');
    pub.diagnostic("%s", fits.c_str());
    TEST_ASSERT_EQUAL_STRING(("# " + fits + "\r\n").c_str(), pop(q).c_str());
    pub.diagnostic("%sx", fits.c_str());
    TEST_ASSERT_EQUAL_STRING("# ERROR: diagnostic line too long, not printed\r\n", pop(q).c_str());
}

// A row that doesn't fit is reported, never printed cut short.
static void test_row_overflow_is_reported() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig(), /*row_capacity=*/20); // the blank row is 36 bytes
    pub.start(); // the header doesn't use row_capacity
    pub.publishRow(MeasurementInputs{}, 0);
    pop(q);
    TEST_ASSERT_EQUAL_STRING("# ERROR: CSV row too long, not printed (1 so far)\r\n", pop(q).c_str());
    TEST_ASSERT_EQUAL_UINT32(1, pub.counters().row_overflows);
    TEST_ASSERT_EQUAL_UINT(0, q.size());
}

static void test_publish_returns_the_snapshot_used() {
    BoundedLineQueue<8> q;
    CsvPublisher pub(q, fixtureConfig());
    MeasurementInputs in;
    in.loadcell.status = LoadCellStatus::Valid;
    in.loadcell.raw = 42;
    in.loadcell.timestamp_ms = 1000;
    const DynoSnapshot s = pub.publishRow(in, 1000);
    TEST_ASSERT_TRUE(s.loadcell_valid);
    TEST_ASSERT_EQUAL_UINT32(1000, s.timestamp_ms);
    TEST_ASSERT_EQUAL(CalcReason::ForceUnavailable, s.calc.torque.reason); // for the "# calc" status line
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_start_queues_the_header_first_and_once);
    RUN_TEST(test_rows_need_no_inputs);
    RUN_TEST(test_full_queue_drops_new_rows_and_counts_them);
    RUN_TEST(test_diagnostic_is_a_whole_hash_line);
    RUN_TEST(test_dropped_diagnostics_are_counted_separately);
    RUN_TEST(test_too_long_diagnostic_is_replaced_not_cut);
    RUN_TEST(test_longest_diagnostic_that_fits);
    RUN_TEST(test_row_overflow_is_reported);
    RUN_TEST(test_publish_returns_the_snapshot_used);
    return UNITY_END();
}
