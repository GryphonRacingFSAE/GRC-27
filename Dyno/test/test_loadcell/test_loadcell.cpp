// Load-cell acquisition (#34), tested with no hardware: a scripted fake
// sensor drives LoadCellAcquirer::step(now_ms) through simulated time.
#include <unity.h>

#include "FakeLoadCellProvider.h"
#include "LoadCellAcquirer.h"

void setUp() {}
void tearDown() {}

// The real defaults from DynoConfig.h: 150 ms deadline, 3 retries, 1 s rest.
static const LoadCellAcquirerConfig kConfig = {150, 3, 1000};

static const uint32_t kPeriod10HzUs = 100000; // 10 Hz: a reading every 100 ms
static const uint32_t kPeriod80HzUs = 12500;  // 80 Hz: a reading every 12.5 ms

static FakeLoadCellScript script(const FakePhase* phases, size_t count, uint32_t period_us,
                                 uint32_t origin_ms = 0, uint32_t repeat_ms = 0) {
    return FakeLoadCellScript{phases, count, period_us, origin_ms, repeat_ms};
}

// Step both fake and acquirer, like the real task does every poll.
static bool stepAt(FakeLoadCellProvider& fake, LoadCellAcquirer& acq, uint32_t now_ms) {
    fake.setTime(now_ms);
    return acq.step(now_ms);
}

// Poll every `poll_ms` over [from_ms, to_ms), the real task's 5 ms cadence.
static void runFor(FakeLoadCellProvider& fake, LoadCellAcquirer& acq, uint32_t from_ms,
                   uint32_t to_ms, uint32_t poll_ms = 5) {
    for (uint32_t t = from_ms; t != to_ms; t += poll_ms) {
        stepAt(fake, acq, t);
    }
}

// ---------------------------------------------------------------------------

static void test_starts_with_no_sample_yet() {
    const FakePhase phases[] = {{0, 10000, FakeMode::NeverReady, 0, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    const LoadCellSample sample = acq.sample();
    TEST_ASSERT_EQUAL(LoadCellStatus::NoSampleYet, sample.status);
    TEST_ASSERT_EQUAL_INT32(0, sample.raw);
    TEST_ASSERT_EQUAL_UINT32(0, sample.timestamp_ms);
}

// Required by #34: 100000 counts ready at 1000 ms -> {100000, 1000, Valid}.
static void test_ready_reading_is_published_with_its_timestamp() {
    // Phase starts at 900 ms; with 100 ms conversions the first one lands at 1000 ms.
    const FakePhase phases[] = {{900, 5000, FakeMode::Ready, 100000, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    TEST_ASSERT_TRUE(stepAt(fake, acq, 1000));

    const LoadCellSample sample = acq.sample();
    TEST_ASSERT_EQUAL_INT32(100000, sample.raw);
    TEST_ASSERT_EQUAL_UINT32(1000, sample.timestamp_ms);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, sample.status);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().samples);
}

// A poll where the chip isn't ready must not zero the reading or move its time.
static void test_not_ready_poll_leaves_sample_unchanged() {
    const FakePhase phases[] = {{900, 5000, FakeMode::Ready, 100000, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    stepAt(fake, acq, 1000);
    TEST_ASSERT_FALSE(stepAt(fake, acq, 1050)); // next reading isn't due until 1100

    const LoadCellSample sample = acq.sample();
    TEST_ASSERT_EQUAL_INT32(100000, sample.raw);
    TEST_ASSERT_EQUAL_UINT32(1000, sample.timestamp_ms);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, sample.status);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().samples);
}

// Reading an HX711 that isn't ready returns garbage, so it must never happen,
// through readings, a dropout, a fault and a recovery.
static void test_never_reads_without_ready() {
    const FakePhase phases[] = {
        {0, 1000, FakeMode::Ready, 5000, 0},
        {1000, 3000, FakeMode::NeverReady, 0, 0},
        {3000, 5000, FakeMode::Ready, 7000, 0},
    };
    const FakeLoadCellScript s = script(phases, 3, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    for (uint32_t t = 0; t < 5000; t += 5) {
        stepAt(fake, acq, t);
    }

    TEST_ASSERT_GREATER_THAN_UINT32(0, fake.reads());
    TEST_ASSERT_EQUAL_UINT32(0, fake.readsWithoutReady());
}

// Pulling on the load cell gives negative counts; they pass through untouched.
static void test_negative_counts_are_preserved() {
    const FakePhase phases[] = {{900, 5000, FakeMode::Ready, -8388608, 0}}; // the chip's minimum
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    stepAt(fake, acq, 1000);

    TEST_ASSERT_EQUAL_INT32(-8388608, acq.sample().raw);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
}

// One missed deadline is a hiccup, not a fault: the reading stays Valid.
static void test_single_timeout_keeps_sample_valid() {
    // One reading at 1000 ms, then silence.
    const FakePhase phases[] = {{900, 1050, FakeMode::Ready, 100000, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 1000, 1145);
    TEST_ASSERT_EQUAL_UINT32(0, acq.counters().timeouts); // 145 ms: still within the deadline

    TEST_ASSERT_FALSE(stepAt(fake, acq, 1150)); // 150 ms after the reading: one timeout

    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().timeouts);
    const LoadCellSample sample = acq.sample();
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, sample.status);
    TEST_ASSERT_EQUAL_INT32(100000, sample.raw);
    TEST_ASSERT_EQUAL_UINT32(1000, sample.timestamp_ms);
}

// Three missed deadlines in a row (450 ms of silence) = Fault. The last good
// reading is kept for reference, and the chip is power-cycled exactly once.
static void test_three_timeouts_enter_fault_and_power_cycle_once() {
    const FakePhase phases[] = {{900, 1050, FakeMode::Ready, 100000, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 1000, 1450);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status); // 2 timeouts so far
    TEST_ASSERT_EQUAL_UINT32(0, fake.powerCycles());

    TEST_ASSERT_TRUE(stepAt(fake, acq, 1450)); // third timeout: status changes

    const LoadCellSample sample = acq.sample();
    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, sample.status);
    TEST_ASSERT_EQUAL_INT32(100000, sample.raw);
    TEST_ASSERT_EQUAL_UINT32(1000, sample.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(3, acq.counters().timeouts);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().faults_entered);
    TEST_ASSERT_EQUAL_UINT32(1, fake.powerCycles());
}

// After the power-cycle the chip gets 1 s of rest: no polls, no timeouts.
static void test_sensor_is_left_alone_during_recovery() {
    const FakePhase phases[] = {{900, 1050, FakeMode::Ready, 100000, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 1000, 1455); // Fault at 1450
    const uint32_t checks_at_fault = fake.readyChecks();
    const uint32_t timeouts_at_fault = acq.counters().timeouts;

    runFor(fake, acq, 1455, 2450); // recovery runs until 1450 + 1000

    TEST_ASSERT_EQUAL_UINT32(checks_at_fault, fake.readyChecks());
    TEST_ASSERT_EQUAL_UINT32(timeouts_at_fault, acq.counters().timeouts);
    TEST_ASSERT_EQUAL_UINT32(1, fake.powerCycles());

    stepAt(fake, acq, 2450); // recovery over: polling resumes
    TEST_ASSERT_EQUAL_UINT32(checks_at_fault + 1, fake.readyChecks());
}

// Sensor comes back: the first good reading clears the Fault, gets its own
// fresh timestamp, and counts as a recovery.
static void test_first_good_reading_after_recovery_clears_fault() {
    const FakePhase phases[] = {
        {900, 1050, FakeMode::Ready, 100000, 0},  // one reading at 1000
        {2000, 9000, FakeMode::Ready, 200000, 0}, // sensor back, readings from 2100
    };
    const FakeLoadCellScript s = script(phases, 2, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 1000, 2450); // Fault at 1450, resting until 2450
    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, acq.sample().status);

    TEST_ASSERT_TRUE(stepAt(fake, acq, 2450));

    const LoadCellSample sample = acq.sample();
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, sample.status);
    TEST_ASSERT_EQUAL_INT32(200000, sample.raw);
    TEST_ASSERT_EQUAL_UINT32(2450, sample.timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().recoveries);
    TEST_ASSERT_EQUAL_UINT32(2, acq.counters().samples);

    // And it is a normal stream again: a timeout now is just a hiccup.
    runFor(fake, acq, 2455, 3000);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().faults_entered);
}

// Sensor disconnects mid-run at 80 Hz. Readings ramp by 1 count per ms so a
// repeated or invented reading would show. Last reading finishes at 987.5 ms
// (worth 987) and is picked up by the 990 ms poll; Fault lands 3 x 150 ms later.
static void test_loss_mid_stream_at_80hz_faults_on_time_without_inventing_samples() {
    const FakePhase phases[] = {{0, 1000, FakeMode::Ready, 0, 1000}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod80HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 0, 1440);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_INT32(987, acq.sample().raw);
    TEST_ASSERT_EQUAL_UINT32(990, acq.sample().timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(79, acq.counters().samples); // one per 12.5 ms conversion

    TEST_ASSERT_TRUE(stepAt(fake, acq, 1440));

    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, acq.sample().status);
    TEST_ASSERT_EQUAL_INT32(987, acq.sample().raw);
    TEST_ASSERT_EQUAL_UINT32(990, acq.sample().timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(79, acq.counters().samples);
}

// Sensor unplugged from power-on: Fault after 450 ms with raw/timestamp 0
// (there never was a good reading), then the rest-and-retry cycle repeats
// with exactly one power-cycle per cycle. Fault is only "entered" once.
static void test_unplugged_from_boot_faults_and_keeps_retrying() {
    const FakePhase phases[] = {{0, 100000, FakeMode::NeverReady, 0, 0}};
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 0, 450);
    TEST_ASSERT_EQUAL(LoadCellStatus::NoSampleYet, acq.sample().status);

    TEST_ASSERT_TRUE(stepAt(fake, acq, 450));
    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, acq.sample().status);
    TEST_ASSERT_EQUAL_INT32(0, acq.sample().raw);
    TEST_ASSERT_EQUAL_UINT32(0, acq.sample().timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(1, fake.powerCycles());

    // Cycle = 1000 ms rest + 3 x 150 ms of tries: power-cycles at 1900 and 3350.
    runFor(fake, acq, 455, 1900);
    TEST_ASSERT_EQUAL_UINT32(1, fake.powerCycles());
    TEST_ASSERT_FALSE(stepAt(fake, acq, 1900)); // already Fault: nothing new to publish
    TEST_ASSERT_EQUAL_UINT32(2, fake.powerCycles());
    runFor(fake, acq, 1905, 3355);
    TEST_ASSERT_EQUAL_UINT32(3, fake.powerCycles());

    TEST_ASSERT_EQUAL_UINT32(9, acq.counters().timeouts);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().faults_entered);
    TEST_ASSERT_EQUAL_UINT32(0, acq.counters().samples);
    TEST_ASSERT_EQUAL_UINT32(0, fake.reads());
}

static bool sameSample(const LoadCellSample& a, const LoadCellSample& b) {
    return a.raw == b.raw && a.timestamp_ms == b.timestamp_ms && a.status == b.status;
}

// step() says "changed" exactly when the sample changed: a new reading or a
// new status. Timeouts, idle polls and resting do not count.
static void test_step_reports_change_only_for_new_reading_or_status() {
    const FakePhase phases[] = {
        {900, 1250, FakeMode::Ready, 10, 1000},  // readings at 1000, 1100, 1200
        {2500, 3000, FakeMode::Ready, 20, 1000}, // readings from 2600 on
    };
    const FakeLoadCellScript s = script(phases, 2, kPeriod10HzUs);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    uint32_t changes = 0;
    for (uint32_t t = 1000; t < 3000; t += 5) {
        const LoadCellSample before = acq.sample();
        const bool changed = stepAt(fake, acq, t);
        TEST_ASSERT_EQUAL(!sameSample(before, acq.sample()), changed);
        changes += changed ? 1 : 0;
    }

    // 3 readings, Fault at 1650, rest until 2650, then readings at 2650
    // (the 2600 one), 2700, 2800, 2900: 3 + 1 + 4.
    TEST_ASSERT_EQUAL_UINT32(8, changes);
    TEST_ASSERT_EQUAL_UINT32(7, acq.counters().samples);
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().recoveries);
}

// The HX711 runs at 10 Hz or 80 Hz depending on how its RATE pin is wired.
// Both must stream for a long time with every reading taken, no timeouts.
static void test_steady_stream_has_no_false_timeouts_at_10hz_and_80hz() {
    const FakePhase phases[] = {{0, 60000, FakeMode::Ready, 0, 1000}};

    const FakeLoadCellScript slow = script(phases, 1, kPeriod10HzUs);
    FakeLoadCellProvider fake10(slow);
    LoadCellAcquirer acq10(fake10, kConfig);
    runFor(fake10, acq10, 0, 60000);
    TEST_ASSERT_EQUAL_UINT32(0, acq10.counters().timeouts);
    TEST_ASSERT_EQUAL_UINT32(599, acq10.counters().samples); // 100 ms .. 59900 ms
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq10.sample().status);

    const FakeLoadCellScript fast = script(phases, 1, kPeriod80HzUs);
    FakeLoadCellProvider fake80(fast);
    LoadCellAcquirer acq80(fake80, kConfig);
    runFor(fake80, acq80, 0, 60000);
    TEST_ASSERT_EQUAL_UINT32(0, acq80.counters().timeouts);
    TEST_ASSERT_EQUAL_UINT32(4799, acq80.counters().samples); // 12.5 ms .. 59987.5 ms
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq80.sample().status);
    TEST_ASSERT_EQUAL_UINT32(0, fake80.readsWithoutReady());
}

// millis() wraps from 4294967295 back to 0 after ~49.7 days. Deadlines and
// the recovery rest must still be measured correctly across the wrap.
// (Both origins are multiples of 5 so the 5 ms polls land on round times.)
static void test_millis_rollover_during_timeouts() {
    const uint32_t origin = 0xFFFFFFFFu - 255u; // wrap happens 256 ms into the script
    const FakePhase phases[] = {{0, 150, FakeMode::Ready, 42, 0}}; // one reading at 100
    const FakeLoadCellScript s = script(phases, 1, kPeriod10HzUs, origin);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, origin, origin + 550u); // timeouts at 250 and 400 (spans the wrap)
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_UINT32(2, acq.counters().timeouts);
    TEST_ASSERT_EQUAL_UINT32(origin + 100u, acq.sample().timestamp_ms);

    TEST_ASSERT_TRUE(stepAt(fake, acq, origin + 550u));
    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, acq.sample().status);
}

static void test_millis_rollover_during_recovery() {
    const uint32_t origin = 0xFFFFFFFFu - 600u; // wrap happens 601 ms into the script
    const FakePhase phases[] = {
        {0, 150, FakeMode::Ready, 42, 0},      // one reading at 100
        {1400, 5000, FakeMode::Ready, 77, 0},  // back again from 1500
    };
    const FakeLoadCellScript s = script(phases, 2, kPeriod10HzUs, origin);
    FakeLoadCellProvider fake(s);
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, origin, origin + 555u); // Fault at 550, just before the wrap
    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, acq.sample().status);
    const uint32_t checks_at_fault = fake.readyChecks();

    runFor(fake, acq, origin + 555u, origin + 1550u); // rest spans the wrap
    TEST_ASSERT_EQUAL_UINT32(checks_at_fault, fake.readyChecks());

    TEST_ASSERT_TRUE(stepAt(fake, acq, origin + 1550u));
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_INT32(77, acq.sample().raw);
    TEST_ASSERT_EQUAL_UINT32(949, acq.sample().timestamp_ms); // origin + 1550, wrapped
}

// The DYNO_FAKE_INPUTS script for a bare devkit: an 80 Hz ramp of 1000
// counts/s from 0, with a 3 s "unplug" from 27 s to 30 s, repeating every 30 s.
static void test_demo_script_ramps_and_unplugs_every_30_s() {
    FakeLoadCellProvider fake(demoLoadCellScript());
    LoadCellAcquirer acq(fake, kConfig);

    runFor(fake, acq, 0, 1000);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_INT32(987, acq.sample().raw); // reading finished at 987.5 ms

    runFor(fake, acq, 1000, 27000);
    TEST_ASSERT_EQUAL_INT32(26987, acq.sample().raw);
    TEST_ASSERT_EQUAL_UINT32(0, acq.counters().timeouts);

    // "Unplugged" at 27 s: last reading 26990, Fault 450 ms later.
    runFor(fake, acq, 27000, 27440);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    runFor(fake, acq, 27440, 30000);
    TEST_ASSERT_EQUAL(LoadCellStatus::Fault, acq.sample().status);

    // "Plugged back in" at 30 s: the ramp restarts and the Fault clears.
    runFor(fake, acq, 30000, 30020);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_INT32(12, acq.sample().raw); // reading at 30012.5 ms
    TEST_ASSERT_EQUAL_UINT32(1, acq.counters().recoveries);

    // And the whole thing repeats.
    runFor(fake, acq, 30020, 60020);
    TEST_ASSERT_EQUAL_UINT32(2, acq.counters().faults_entered);
    TEST_ASSERT_EQUAL_UINT32(2, acq.counters().recoveries);
    TEST_ASSERT_EQUAL(LoadCellStatus::Valid, acq.sample().status);
    TEST_ASSERT_EQUAL_UINT32(0, fake.readsWithoutReady());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_starts_with_no_sample_yet);
    RUN_TEST(test_ready_reading_is_published_with_its_timestamp);
    RUN_TEST(test_not_ready_poll_leaves_sample_unchanged);
    RUN_TEST(test_never_reads_without_ready);
    RUN_TEST(test_negative_counts_are_preserved);
    RUN_TEST(test_single_timeout_keeps_sample_valid);
    RUN_TEST(test_three_timeouts_enter_fault_and_power_cycle_once);
    RUN_TEST(test_sensor_is_left_alone_during_recovery);
    RUN_TEST(test_first_good_reading_after_recovery_clears_fault);
    RUN_TEST(test_loss_mid_stream_at_80hz_faults_on_time_without_inventing_samples);
    RUN_TEST(test_unplugged_from_boot_faults_and_keeps_retrying);
    RUN_TEST(test_step_reports_change_only_for_new_reading_or_status);
    RUN_TEST(test_steady_stream_has_no_false_timeouts_at_10hz_and_80hz);
    RUN_TEST(test_millis_rollover_during_timeouts);
    RUN_TEST(test_millis_rollover_during_recovery);
    RUN_TEST(test_demo_script_ramps_and_unplugs_every_30_s);
    return UNITY_END();
}
