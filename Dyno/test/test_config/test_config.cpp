// Invariants on the hardware placeholders. These must keep holding when the
// TBD values in pindefs.h / DynoConfig.h are replaced with real ones.
#include <string.h>
#include <unity.h>

#include "CalibrationExamples.h"
#include "DynoCalcSetup.h"
#include "DynoCalibrationSetup.h"
#include "LineReader.h"
#include "DynoConfig.h"
#include "TorquePower.h"
#include "TorquePowerExamples.h"
#include "pindefs.h"

void setUp() {}
void tearDown() {}

static void test_all_dyno_pins_are_distinct() {
    TEST_ASSERT_TRUE(dynoPinsAreDistinct());
}

// HX711 runs at 10 Hz or 80 Hz depending on its RATE pin; the readiness
// deadline must cover a full 10 Hz conversion period (100 ms).
static void test_hx711_deadline_covers_slowest_sample_rate() {
    TEST_ASSERT_GREATER_THAN_UINT32(100, HX711_READY_DEADLINE_MS);
}

// Issue #37: unresolved real geometry/speed information cannot enable
// physical output. With a perfect force and RPM, torque is valid only once the
// geometry is confirmed, and power only once the same shaft is confirmed too.
// (Today both are TBD, so neither is valid.)
static void test_real_setup_gives_output_only_when_confirmed() {
    ForceMeasurement force;
    force.available = true;
    force.force_n = 100.0;
    force.timestamp_ms = 1000;
    force.calibration_ready = true;
    ShaftSpeedSample speed;
    speed.received = true;
    speed.rpm = 600.0;
    speed.timestamp_ms = 1000;

    const TorquePowerResult r = computeTorquePower(force, speed, dynoMechanicalSetup(), dynoCalcLimits(), 1000);
    TEST_ASSERT_EQUAL(GEOMETRY_CONFIRMED, r.torque.valid);
    TEST_ASSERT_EQUAL(GEOMETRY_CONFIRMED && SAME_SHAFT_CONFIRMED, r.power.valid);
    if (!GEOMETRY_CONFIRMED) {
        TEST_ASSERT_EQUAL(CalcReason::GeometryUnconfirmed, r.torque.reason);
    }
}

// #38's example dataset uses the same limits as the firmware. If a limit is
// retuned here, the dataset's boundary rows must be reworked too.
static void test_example_dataset_limits_match_config() {
    const CalcLimits real = dynoCalcLimits();
    TEST_ASSERT_EQUAL_UINT32(real.force_freshness_ms, kExampleLimits.force_freshness_ms);
    TEST_ASSERT_EQUAL_UINT32(real.rpm_freshness_ms, kExampleLimits.rpm_freshness_ms);
    TEST_ASSERT_EQUAL_UINT32(real.max_source_skew_ms, kExampleLimits.max_source_skew_ms);
    TEST_ASSERT_EQUAL_DOUBLE(real.max_arm_m, kExampleLimits.max_arm_m);
}

// #35's host tests are written against CalibrationExamples.h. If a setting
// in DynoConfig.h is retuned, rework those tests' timings, then the fixture.
static void test_calibration_fixture_matches_config() {
    const CalibratorConfig real = dynoCalibratorConfig();
    const CalibratorConfig& ex = kExampleCalibratorConfig;
    TEST_ASSERT_EQUAL_UINT32(real.window.window_ms, ex.window.window_ms);
    TEST_ASSERT_EQUAL_UINT32(real.window.min_samples, ex.window.min_samples);
    TEST_ASSERT_EQUAL_INT32(real.window.max_spread_counts, ex.window.max_spread_counts);
    TEST_ASSERT_EQUAL_UINT32(real.window.timeout_ms, ex.window.timeout_ms);
    TEST_ASSERT_EQUAL_INT32(real.window.saturation_counts, ex.window.saturation_counts);
    TEST_ASSERT_EQUAL_DOUBLE(real.min_separation_counts, ex.min_separation_counts);
}

// The stability window must hold a whole window of samples at the HX711's
// fastest rate (80 Hz), or it would quietly judge a shorter span.
static void test_calibration_window_fits_the_sample_buffer() {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(StableWindow::kMaxSamples, CAL_WINDOW_MS * 80 / 1000);
}

// "cal loaded 1234.567kg" plus spaces must fit a line.
static void test_command_line_fits_the_longest_command() {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(strlen("cal loaded 1234.567kg") + 8, LineReader::kMaxLine);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_dyno_pins_are_distinct);
    RUN_TEST(test_hx711_deadline_covers_slowest_sample_rate);
    RUN_TEST(test_real_setup_gives_output_only_when_confirmed);
    RUN_TEST(test_example_dataset_limits_match_config);
    RUN_TEST(test_calibration_fixture_matches_config);
    RUN_TEST(test_calibration_window_fits_the_sample_buffer);
    RUN_TEST(test_command_line_fits_the_longest_command);
    return UNITY_END();
}
