// Calibration maths (#35), tested with no hardware. Expected numbers are typed
// in from issue #35 or worked out by hand, never recomputed the way the code
// does it.
#include <math.h>
#include <unity.h>

#include "CalibrationMath.h"

void setUp() {}
void tearDown() {}

#define ASSERT_CLOSE(expected, actual) \
    TEST_ASSERT_DOUBLE_WITHIN(fmax(1e-9, fabs(expected) * 1e-9), (expected), (actual))

// The issue's separation is 20000 counts; use a limit well under it.
static const double kMinSeparation = 5000.0;

static CalibrationRecord recordFrom(double unloaded, double loaded, double reference_n) {
    const ScaleResult s = scaleFromPoints(unloaded, loaded, reference_n, kMinSeparation);
    TEST_ASSERT_EQUAL(CalibrationError::None, s.error);
    CalibrationRecord r;
    r.valid = true;
    r.offset_counts = unloaded;
    r.counts_per_n = s.counts_per_n;
    return r;
}

// ---------------------------------------------------------------------------
// Issue #35 worked example

static void test_issue_example_gives_200_counts_per_newton_and_50_n() {
    const ScaleResult s = scaleFromPoints(100000.0, 120000.0, 100.0, kMinSeparation);
    TEST_ASSERT_EQUAL(CalibrationError::None, s.error);
    ASSERT_CLOSE(200.0, s.counts_per_n);

    const ForceConversion f = forceFromRaw(recordFrom(100000.0, 120000.0, 100.0), 110000);
    TEST_ASSERT_TRUE(f.valid);
    ASSERT_CLOSE(50.0, f.force_n);
}

// Loaded 80000 under the same positive 100 N: -200 counts/N is a valid
// signed scale (issue #35), and force keeps the reference load's direction.
static void test_reversed_count_direction_is_a_valid_negative_scale() {
    const ScaleResult s = scaleFromPoints(100000.0, 80000.0, 100.0, kMinSeparation);
    TEST_ASSERT_EQUAL(CalibrationError::None, s.error);
    ASSERT_CLOSE(-200.0, s.counts_per_n);

    const CalibrationRecord r = recordFrom(100000.0, 80000.0, 100.0);
    ASSERT_CLOSE(50.0, forceFromRaw(r, 90000).force_n);   // pushed the same way as the reference
    ASSERT_CLOSE(-50.0, forceFromRaw(r, 110000).force_n); // pushed the other way
}

static void test_zero_and_negative_force_are_valid_numbers() {
    const CalibrationRecord r = recordFrom(100000.0, 120000.0, 100.0);
    const ForceConversion zero = forceFromRaw(r, 100000);
    TEST_ASSERT_TRUE(zero.valid);
    ASSERT_CLOSE(0.0, zero.force_n);
    const ForceConversion negative = forceFromRaw(r, 90000);
    TEST_ASSERT_TRUE(negative.valid);
    ASSERT_CLOSE(-50.0, negative.force_n);
}

// The HX711's extremes (24-bit: -8388608 .. 8388607) convert without overflow.
static void test_largest_raw_values_convert() {
    const CalibrationRecord r = recordFrom(100000.0, 120000.0, 100.0);
    ASSERT_CLOSE(41443.035, forceFromRaw(r, 8388607).force_n);   // (8388607 - 100000) / 200
    ASSERT_CLOSE(-42443.04, forceFromRaw(r, -8388608).force_n);  // (-8388608 - 100000) / 200
    // And the full int32 range, in case a fake or a future chip sends it.
    ASSERT_CLOSE(10736918.235, forceFromRaw(r, INT32_MAX).force_n); // (2147483647 - 100000) / 200
}

// A fractional offset (a window mean) is kept, not rounded.
static void test_fractional_offset_is_kept() {
    CalibrationRecord r = recordFrom(100000.0, 120000.0, 100.0);
    r.offset_counts = 100000.5;
    ASSERT_CLOSE(-0.0025, forceFromRaw(r, 100000).force_n);
}

// ---------------------------------------------------------------------------
// Missing / unusable calibration: invalid, never a plausible 0 N.

static void test_default_record_is_not_a_calibration() {
    const CalibrationRecord none;
    TEST_ASSERT_FALSE(calibrationUsable(none));
    const ForceConversion f = forceFromRaw(none, 110000);
    TEST_ASSERT_FALSE(f.valid);
    TEST_ASSERT_TRUE(isnan(f.force_n));
}

static void test_record_with_bad_numbers_is_unusable() {
    const CalibrationRecord good = recordFrom(100000.0, 120000.0, 100.0);
    TEST_ASSERT_TRUE(calibrationUsable(good));

    CalibrationRecord r = good;
    r.valid = false;
    TEST_ASSERT_FALSE(forceFromRaw(r, 110000).valid);
    r = good;
    r.counts_per_n = 0.0;
    TEST_ASSERT_FALSE(forceFromRaw(r, 110000).valid);
    const double bad[] = {NAN, INFINITY, -INFINITY};
    for (double b : bad) {
        r = good;
        r.counts_per_n = b;
        TEST_ASSERT_FALSE(forceFromRaw(r, 110000).valid);
        r = good;
        r.offset_counts = b;
        TEST_ASSERT_FALSE(calibrationUsable(r)); // also vets records loaded from storage
        TEST_ASSERT_FALSE(forceFromRaw(r, 110000).valid);
    }
}

// A tiny but non-zero scale can push force past what a double holds.
static void test_force_overflowing_to_infinity_is_invalid() {
    CalibrationRecord r = recordFrom(100000.0, 120000.0, 100.0);
    r.counts_per_n = 1e-320; // denormal: 1e7 / 1e-320 = infinity
    TEST_ASSERT_FALSE(forceFromRaw(r, 10100000).valid);
}

// ---------------------------------------------------------------------------
// Candidate validation (issue #35 step 5)

static void test_bad_reference_force_is_rejected() {
    const double bad[] = {0.0, -100.0, NAN, INFINITY, -INFINITY};
    for (double f : bad) {
        TEST_ASSERT_EQUAL(CalibrationError::ReferenceForceInvalid,
                          scaleFromPoints(100000.0, 120000.0, f, kMinSeparation).error);
    }
}

static void test_non_finite_counts_are_rejected() {
    TEST_ASSERT_EQUAL(CalibrationError::CountsInvalid, scaleFromPoints(NAN, 120000.0, 100.0, kMinSeparation).error);
    TEST_ASSERT_EQUAL(CalibrationError::CountsInvalid,
                      scaleFromPoints(100000.0, INFINITY, 100.0, kMinSeparation).error);
}

// Separation limit 5000 counts: 4999 apart is noise, 5000 is accepted, in
// either direction.
static void test_near_zero_separation_is_rejected_at_the_limit() {
    TEST_ASSERT_EQUAL(CalibrationError::SeparationTooSmall,
                      scaleFromPoints(100000.0, 100000.0, 100.0, kMinSeparation).error);
    TEST_ASSERT_EQUAL(CalibrationError::SeparationTooSmall,
                      scaleFromPoints(100000.0, 104999.0, 100.0, kMinSeparation).error);
    TEST_ASSERT_EQUAL(CalibrationError::SeparationTooSmall,
                      scaleFromPoints(100000.0, 95001.0, 100.0, kMinSeparation).error);

    const ScaleResult up = scaleFromPoints(100000.0, 105000.0, 100.0, kMinSeparation);
    TEST_ASSERT_EQUAL(CalibrationError::None, up.error);
    ASSERT_CLOSE(50.0, up.counts_per_n);
    const ScaleResult down = scaleFromPoints(100000.0, 95000.0, 100.0, kMinSeparation);
    TEST_ASSERT_EQUAL(CalibrationError::None, down.error);
    ASSERT_CLOSE(-50.0, down.counts_per_n);
}

// A reference force so small the scale overflows: rejected, not infinite.
static void test_scale_overflowing_to_infinity_is_rejected() {
    TEST_ASSERT_EQUAL(CalibrationError::ScaleInvalid,
                      scaleFromPoints(100000.0, 120000.0, 1e-320, kMinSeparation).error);
}

// ---------------------------------------------------------------------------
// Mass -> force, once (issue #35: F = m * g, g recorded)

static void test_mass_converts_with_standard_gravity() {
    ASSERT_CLOSE(9.80665, kStandardGravityMs2);
    ASSERT_CLOSE(100.027830, newtonsFromKilograms(10.2, kStandardGravityMs2)); // 10.2 * 9.80665
}

static void test_error_names() {
    TEST_ASSERT_EQUAL_STRING("SeparationTooSmall", calibrationErrorName(CalibrationError::SeparationTooSmall));
    TEST_ASSERT_EQUAL_STRING("ReferenceForceInvalid", calibrationErrorName(CalibrationError::ReferenceForceInvalid));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_issue_example_gives_200_counts_per_newton_and_50_n);
    RUN_TEST(test_reversed_count_direction_is_a_valid_negative_scale);
    RUN_TEST(test_zero_and_negative_force_are_valid_numbers);
    RUN_TEST(test_largest_raw_values_convert);
    RUN_TEST(test_fractional_offset_is_kept);
    RUN_TEST(test_default_record_is_not_a_calibration);
    RUN_TEST(test_record_with_bad_numbers_is_unusable);
    RUN_TEST(test_force_overflowing_to_infinity_is_invalid);
    RUN_TEST(test_bad_reference_force_is_rejected);
    RUN_TEST(test_non_finite_counts_are_rejected);
    RUN_TEST(test_near_zero_separation_is_rejected_at_the_limit);
    RUN_TEST(test_scale_overflowing_to_infinity_is_rejected);
    RUN_TEST(test_mass_converts_with_standard_gravity);
    RUN_TEST(test_error_names);
    return UNITY_END();
}
