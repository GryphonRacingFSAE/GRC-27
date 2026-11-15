// Stable-window sample collection (#35 step 4), tested with a fake clock and
// hand-made samples. Means are worked out by hand.
#include <unity.h>

#include "CalibrationExamples.h"
#include "StableWindow.h"

void setUp() {}
void tearDown() {}

// FIXTURE (CalibrationExamples.h; test_config pins it to DynoConfig.h): mean
// of the last 1 s (>= 8 samples), stable when max - min <= 500 counts, give
// up after 10 s, saturated at |raw| >= 8388000.
static const StableWindowConfig kRule = kExampleCalibratorConfig.window;

static LoadCellSample valid(int32_t raw, uint32_t t_ms) {
    LoadCellSample s;
    s.raw = raw;
    s.timestamp_ms = t_ms;
    s.status = LoadCellStatus::Valid;
    return s;
}

// Feeds one fresh sample every period_ms from `from` (inclusive) to `to`
// (exclusive), raw = value(t). Stops early if the window finishes.
template <typename F>
static WindowStatus feed(StableWindow& w, uint32_t from, uint32_t to, uint32_t period_ms, F value) {
    WindowStatus st = WindowStatus::Collecting;
    for (uint32_t t = from; t != to; t += period_ms) {
        st = w.step(valid(value(t), t), t);
        if (st != WindowStatus::Collecting) break;
    }
    return st;
}

// 10 Hz, alternating 99900 / 100100 (spread 200): stable as soon as a full
// second has been collected, mean exactly 100000.
static void test_steady_10hz_signal_is_stable_after_one_window() {
    StableWindow w(kRule);
    w.start(0);
    const WindowStatus st = feed(w, 0, 5000, 100, [](uint32_t t) { return (t / 100) % 2 ? 100100 : 99900; });
    TEST_ASSERT_EQUAL(WindowStatus::Stable, st);
    const WindowResult r = w.result();
    TEST_ASSERT_EQUAL_UINT32(1000, r.finished_ms);
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, r.mean_counts);
    TEST_ASSERT_EQUAL_INT32(200, r.spread_counts);
}

// 80 Hz (12.5 ms; fed as 12/13 ms steps): also stable at 1 s, on the samples
// of that last second only.
static void test_80hz_signal_is_stable_after_one_window() {
    StableWindow w(kRule);
    w.start(0);
    WindowStatus st = WindowStatus::Collecting;
    uint32_t t = 0;
    for (int i = 0; st == WindowStatus::Collecting && t < 3000; ++i) {
        st = w.step(valid(250000, t), t);
        t += (i % 2) ? 13 : 12;
    }
    TEST_ASSERT_EQUAL(WindowStatus::Stable, st);
    const WindowResult r = w.result();
    TEST_ASSERT_TRUE(r.finished_ms >= 1000 && r.finished_ms < 1013);
    TEST_ASSERT_TRUE(r.samples >= 79 && r.samples <= 81);
    TEST_ASSERT_EQUAL_DOUBLE(250000.0, r.mean_counts);
}

// The weight is being hung on for the first 3 s (big swings), then settles at
// 120000 +/- 100. Stable once a whole second of settled data is in the
// window; the swings are not in the mean.
static void test_settling_load_becomes_stable_on_settled_samples_only() {
    StableWindow w(kRule);
    w.start(0);
    const WindowStatus st = feed(w, 0, 10000, 100, [](uint32_t t) -> int32_t {
        if (t < 3000) return (t / 100) % 2 ? 140000 : 100000;
        return (t / 100) % 2 ? 120100 : 119900;
    });
    TEST_ASSERT_EQUAL(WindowStatus::Stable, st);
    const WindowResult r = w.result();
    TEST_ASSERT_EQUAL_UINT32(3900, r.finished_ms); // window 3000..3900 is the first all-settled one
    TEST_ASSERT_EQUAL_DOUBLE(120000.0, r.mean_counts);
}

// Spread limit 500: exactly 500 is accepted...
static void test_spread_at_limit_is_stable() {
    StableWindow w(kRule);
    w.start(0);
    TEST_ASSERT_EQUAL(WindowStatus::Stable,
                      feed(w, 0, 5000, 100, [](uint32_t t) { return (t / 100) % 2 ? 100500 : 100000; }));
    TEST_ASSERT_EQUAL_INT32(500, w.result().spread_counts);
    TEST_ASSERT_EQUAL_DOUBLE(100250.0, w.result().mean_counts);
}

// ...501 never is: Unstable at exactly the 10 s timeout, not before.
static void test_spread_over_limit_times_out_unstable() {
    StableWindow w(kRule);
    w.start(0);
    // Samples at 0..9900 ms.
    TEST_ASSERT_EQUAL(WindowStatus::Collecting,
                      feed(w, 0, 10000, 100, [](uint32_t t) { return (t / 100) % 2 ? 100501 : 100000; }));
    const LoadCellSample last = valid(100000, 9900); // nothing new after it
    TEST_ASSERT_EQUAL(WindowStatus::Collecting, w.step(last, 9999)); // just under the timeout
    TEST_ASSERT_EQUAL(WindowStatus::Failed, w.step(last, 10000));
    const WindowResult r = w.result();
    TEST_ASSERT_EQUAL(WindowFailure::Unstable, r.failure);
    TEST_ASSERT_EQUAL_UINT32(10000, r.finished_ms);
    TEST_ASSERT_EQUAL_INT32(501, r.spread_counts);
}

// Readings at 5 Hz: only 5 in any second, below the 8 needed.
static void test_too_slow_samples_time_out_insufficient() {
    StableWindow w(kRule);
    w.start(0);
    WindowStatus st = WindowStatus::Collecting;
    for (uint32_t t = 0; t <= 10000; t += 10) {
        st = w.step(valid(100000, t - t % 200), t); // a new sample every 200 ms
        if (st != WindowStatus::Collecting) break;
    }
    TEST_ASSERT_EQUAL(WindowStatus::Failed, st);
    TEST_ASSERT_EQUAL(WindowFailure::InsufficientSamples, w.result().failure);
    TEST_ASSERT_EQUAL_UINT32(10000, w.result().finished_ms);
}

// The same sample fed over and over (the HX711 stalled, but not yet a Fault)
// counts once, so it can't fake a full window.
static void test_repeated_sample_counts_once() {
    StableWindow w(kRule);
    w.start(0);
    for (uint32_t t = 0; t <= 10000; t += 5) {
        w.step(valid(100000, 0), t);
    }
    TEST_ASSERT_EQUAL(WindowStatus::Failed, w.result().status);
    TEST_ASSERT_EQUAL(WindowFailure::InsufficientSamples, w.result().failure);
}

// Data stops arriving after 5 samples: they age out of the window and the
// count never reaches 8. (In the firmware a stalled HX711 also becomes a
// Fault within ~450 ms, which fails the collection at once.)
static void test_samples_that_stop_arriving_age_out() {
    StableWindow w(kRule);
    w.start(0);
    feed(w, 0, 500, 100, [](uint32_t) { return 100000; }); // 5 samples, 0..400
    for (uint32_t t = 500; t <= 10000; t += 10) {
        w.step(valid(100000, 400), t);
    }
    TEST_ASSERT_EQUAL(WindowFailure::InsufficientSamples, w.result().failure);
    TEST_ASSERT_EQUAL_UINT32(0, w.result().samples);
}

// A sample taken before start() (e.g. the reading before the weight went on)
// is not used.
static void test_sample_from_before_start_is_ignored() {
    StableWindow w(kRule);
    w.start(5000);
    for (uint32_t t = 5000; t <= 15000; t += 10) {
        w.step(valid(100000, 4900), t);
    }
    TEST_ASSERT_EQUAL(WindowFailure::InsufficientSamples, w.result().failure);
}

static void test_sensor_fault_fails_at_once() {
    StableWindow w(kRule);
    w.start(0);
    feed(w, 0, 500, 100, [](uint32_t) { return 100000; });
    LoadCellSample fault = valid(100000, 400); // Fault keeps the last good raw
    fault.status = LoadCellStatus::Fault;
    TEST_ASSERT_EQUAL(WindowStatus::Failed, w.step(fault, 550));
    TEST_ASSERT_EQUAL(WindowFailure::SensorFault, w.result().failure);
    TEST_ASSERT_EQUAL_UINT32(550, w.result().finished_ms);
}

// No sample at all yet: just keeps waiting (then times out).
static void test_no_sample_yet_waits() {
    StableWindow w(kRule);
    w.start(0);
    const LoadCellSample none;
    TEST_ASSERT_EQUAL(WindowStatus::Collecting, w.step(none, 9999));
    TEST_ASSERT_EQUAL(WindowStatus::Failed, w.step(none, 10000));
    TEST_ASSERT_EQUAL(WindowFailure::InsufficientSamples, w.result().failure);
}

// |raw| >= 8388000 is saturated (the HX711 clamps at +8388607/-8388608, and an
// unplugged bridge reads near the rails). 8387999 is fine.
static void test_saturated_sample_fails_at_once() {
    const int32_t saturated[] = {8388000, 8388607, -8388000, -8388608};
    for (int32_t raw : saturated) {
        StableWindow w(kRule);
        w.start(0);
        w.step(valid(100000, 0), 0);
        TEST_ASSERT_EQUAL(WindowStatus::Failed, w.step(valid(raw, 100), 100));
        TEST_ASSERT_EQUAL(WindowFailure::Saturated, w.result().failure);
    }
    StableWindow ok(kRule);
    ok.start(0);
    TEST_ASSERT_EQUAL(WindowStatus::Stable, feed(ok, 0, 5000, 100, [](uint32_t t) {
                          return (t / 100) % 2 ? 8387999 : 8387600;
                      }));
    TEST_ASSERT_EQUAL(WindowStatus::Stable, ok.step(valid(0, 6000), 6000)); // latched: later samples change nothing
    TEST_ASSERT_EQUAL_DOUBLE(8387799.5, ok.result().mean_counts);
}

// Large negative readings (a reversed load cell) average correctly.
static void test_negative_readings_average_correctly() {
    StableWindow w(kRule);
    w.start(0);
    TEST_ASSERT_EQUAL(WindowStatus::Stable,
                      feed(w, 0, 5000, 100, [](uint32_t t) { return (t / 100) % 2 ? -8000100 : -8000300; }));
    TEST_ASSERT_EQUAL_DOUBLE(-8000200.0, w.result().mean_counts);
}

// millis() wraps every ~49.7 days; a collection across the wrap still works.
static void test_works_across_millis_rollover() {
    const uint32_t start = 0xFFFFFE00u; // 512 ms before the wrap
    StableWindow w(kRule);
    w.start(start);
    const WindowStatus st = feed(w, start, start + 5000, 100, [](uint32_t) { return 100000; });
    TEST_ASSERT_EQUAL(WindowStatus::Stable, st);
    TEST_ASSERT_EQUAL_UINT32(start + 1000, w.result().finished_ms);
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, w.result().mean_counts);
}

// The result stays latched; start() begins a fresh collection, cancel()
// goes back to Idle.
static void test_restart_and_cancel() {
    StableWindow w(kRule);
    TEST_ASSERT_EQUAL(WindowStatus::Idle, w.result().status);
    TEST_ASSERT_EQUAL(WindowStatus::Idle, w.step(valid(1, 0), 0)); // not started: does nothing
    w.start(0);
    feed(w, 0, 5000, 100, [](uint32_t) { return 100000; });
    TEST_ASSERT_EQUAL(WindowStatus::Stable, w.step(valid(5, 6000), 6000));
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, w.result().mean_counts);

    w.start(7000); // old samples must not carry over
    TEST_ASSERT_EQUAL(WindowStatus::Stable, feed(w, 7000, 12000, 100, [](uint32_t) { return 200000; }));
    TEST_ASSERT_EQUAL_DOUBLE(200000.0, w.result().mean_counts);

    w.start(13000);
    TEST_ASSERT_TRUE(w.collecting());
    w.cancel();
    TEST_ASSERT_FALSE(w.collecting());
    TEST_ASSERT_EQUAL(WindowStatus::Idle, w.result().status);
}

// Faster than the buffer (200 Hz = 200 samples/s, room for 128): the oldest
// drop out and the window holds the newest 128, still all the same value.
static void test_more_samples_than_the_buffer_keeps_the_newest() {
    StableWindow w(kRule);
    w.start(0);
    TEST_ASSERT_EQUAL(WindowStatus::Stable, feed(w, 0, 3000, 5, [](uint32_t t) { return t < 300 ? 0 : 100000; }));
    TEST_ASSERT_EQUAL_UINT32(1000, w.result().finished_ms);
    TEST_ASSERT_EQUAL_UINT32(128, w.result().samples);
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, w.result().mean_counts);
}

static void test_failure_names() {
    TEST_ASSERT_EQUAL_STRING("Saturated", windowFailureName(WindowFailure::Saturated));
    TEST_ASSERT_EQUAL_STRING("InsufficientSamples", windowFailureName(WindowFailure::InsufficientSamples));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_steady_10hz_signal_is_stable_after_one_window);
    RUN_TEST(test_80hz_signal_is_stable_after_one_window);
    RUN_TEST(test_settling_load_becomes_stable_on_settled_samples_only);
    RUN_TEST(test_spread_at_limit_is_stable);
    RUN_TEST(test_spread_over_limit_times_out_unstable);
    RUN_TEST(test_too_slow_samples_time_out_insufficient);
    RUN_TEST(test_repeated_sample_counts_once);
    RUN_TEST(test_samples_that_stop_arriving_age_out);
    RUN_TEST(test_sample_from_before_start_is_ignored);
    RUN_TEST(test_sensor_fault_fails_at_once);
    RUN_TEST(test_no_sample_yet_waits);
    RUN_TEST(test_saturated_sample_fails_at_once);
    RUN_TEST(test_negative_readings_average_correctly);
    RUN_TEST(test_works_across_millis_rollover);
    RUN_TEST(test_restart_and_cancel);
    RUN_TEST(test_more_samples_than_the_buffer_keeps_the_newest);
    RUN_TEST(test_failure_names);
    return UNITY_END();
}
