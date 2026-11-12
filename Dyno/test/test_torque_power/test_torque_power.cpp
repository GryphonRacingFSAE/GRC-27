// Torque and power (#37), tested with no hardware. Every expected number is
// typed in from issue #37 or worked out by hand, never recomputed the way the
// code does it.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "EcuDecoder.h"
#include "TorquePower.h"
#include "TorquePowerExamples.h"

void setUp() {}
void tearDown() {}

// Relative tolerance of 1e-6 (the issue: "small numerical tolerances").
#define ASSERT_CLOSE(expected, actual) \
    TEST_ASSERT_DOUBLE_WITHIN(fmax(1e-9, fabs(expected) * 1e-6), (expected), (actual))

// FIXTURE ONLY: the issue's example geometry (perpendicular 0.25 m arm, same
// shaft). Not the real dyno; the real values live in DynoConfig.h.
static const MechanicalSetup kFixtureSetup = {0.25, true, true};
// The agreed limits: force < 500 ms old, RPM < 2000 ms old (CAN_FRESHNESS_MS),
// force/RPM timestamps at most 250 ms apart, arm at most 2.0 m. Pinned to
// those numbers by test_example_limits_are_the_agreed_values, and to
// DynoConfig.h by test_config.
static const CalcLimits& kLimits = kExampleLimits;

static ForceMeasurement force(double newtons, uint32_t t_ms = 1000) {
    ForceMeasurement f;
    f.available = true;
    f.force_n = newtons;
    f.timestamp_ms = t_ms;
    f.calibration_ready = true;
    f.calibration_version = 3;
    f.tare_version = 5;
    return f;
}

static ShaftSpeedSample rpm(double value, uint32_t t_ms = 1000) {
    ShaftSpeedSample s;
    s.received = true;
    s.rpm = value;
    s.timestamp_ms = t_ms;
    return s;
}

// ---------------------------------------------------------------------------
// Seam A: the maths helpers

static void test_helpers_match_the_issue_reference_calculation() {
    ASSERT_CLOSE(25.0, torqueNm(100.0, 0.25));
    ASSERT_CLOSE(62.831853, radPerSFromRpm(600.0));
    ASSERT_CLOSE(1570.796327, powerW(25.0, 62.831853072));
    ASSERT_CLOSE(1.570796, kilowattsFromWatts(1570.796327));
    ASSERT_CLOSE(2.106473, mechanicalHpFromWatts(1570.796327));
}

// ---------------------------------------------------------------------------
// Seam B: computeTorquePower
//
// The issue #37 test table (reference case, zero force, zero rpm, -100 N,
// missing/stale RPM, unconfirmed geometry/shaft, 250 m arm, not-ready,
// unavailable and NaN force, version blanking) is the example dataset in
// TorquePowerExamples.h, checked row by row in Seam D below. The tests here
// cover what the dataset doesn't: limit boundaries, rollover, check order.

// ---------------------------------------------------------------------------
// Invalid results: every value is NaN (so a missing result can never pass for
// a real zero) and the reason says why.

static void assertTorqueInvalid(const TorquePowerResult& r, CalcReason reason) {
    TEST_ASSERT_FALSE(r.torque.valid);
    TEST_ASSERT_EQUAL(reason, r.torque.reason);
    TEST_ASSERT_TRUE(isnan(r.torque.torque_nm));
    TEST_ASSERT_FALSE(r.reverse_torque);
}

static void assertPowerInvalid(const TorquePowerResult& r, CalcReason reason) {
    TEST_ASSERT_FALSE(r.power.valid);
    TEST_ASSERT_EQUAL(reason, r.power.reason);
    TEST_ASSERT_TRUE(isnan(r.power.power_w));
    TEST_ASSERT_TRUE(isnan(r.power.power_kw));
    TEST_ASSERT_TRUE(isnan(r.power.horsepower_hp));
}

// Torque and power both invalid for the same reason (power needs torque).
static void assertBothInvalid(const TorquePowerResult& r, CalcReason reason) {
    assertTorqueInvalid(r, reason);
    assertPowerInvalid(r, reason);
}

static TorquePowerResult withArm(double arm_m) {
    const MechanicalSetup setup = {arm_m, true, true};
    return computeTorquePower(force(100.0), rpm(600.0), setup, kLimits, 1000);
}

// ---------------------------------------------------------------------------
// Units and geometry

// Issue: "a unit-conversion check distinguishing 250 millimetres from 250 metres".
static void test_250_millimetres_is_a_quarter_metre() {
    ASSERT_CLOSE(0.25, metresFromMillimetres(250.0));
    ASSERT_CLOSE(250.0, metresFromMillimetres(250000.0));
}

static void test_arm_from_millimetres_gives_the_reference_torque() {
    const TorquePowerResult r = withArm(metresFromMillimetres(250.0));
    TEST_ASSERT_TRUE(r.torque.valid);
    ASSERT_CLOSE(25.0, r.torque.torque_nm);
}

static void test_arm_at_the_2_metre_limit_is_accepted() {
    const TorquePowerResult r = withArm(2.0);
    TEST_ASSERT_TRUE(r.torque.valid);
    ASSERT_CLOSE(200.0, r.torque.torque_nm); // 100 N x 2.0 m
}

static void test_arm_just_over_the_2_metre_limit_is_rejected() {
    assertBothInvalid(withArm(2.001), CalcReason::ArmInvalid);
}

static void test_impossible_arms_are_rejected() {
    const double arms[] = {0.0, -0.25, NAN, INFINITY};
    for (double arm : arms) {
        assertBothInvalid(withArm(arm), CalcReason::ArmInvalid);
    }
}

// Issue table: "Unknown geometry -> no claimed physical result that depends on it".
// A plausible arm that isn't confirmed is still refused. The versions still
// describe the force, which is fine; only the geometry isn't.
static void test_unconfirmed_geometry_gives_no_torque_or_power() {
    const MechanicalSetup setup = {0.25, false, true};
    const TorquePowerResult r = computeTorquePower(force(100.0), rpm(600.0), setup, kLimits, 1000);
    assertBothInvalid(r, CalcReason::GeometryUnconfirmed);
    TEST_ASSERT_TRUE(r.versions_known);
    TEST_ASSERT_EQUAL_UINT32(3, r.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(5, r.tare_version);
}

// ---------------------------------------------------------------------------
// Bad force. Issue table: "Missing/stale force, invalid calibration,
// NaN/infinity -> invalid dependent outputs with a reason".

static TorquePowerResult withForce(const ForceMeasurement& f) {
    return computeTorquePower(f, rpm(600.0), kFixtureSetup, kLimits, 1000);
}

static void test_default_force_is_unavailable_not_zero() {
    assertBothInvalid(withForce(ForceMeasurement{}), CalcReason::ForceUnavailable);
}

static void test_non_finite_force_is_rejected() {
    const double values[] = {NAN, INFINITY, -INFINITY};
    for (double n : values) {
        const TorquePowerResult r = withForce(force(n));
        assertBothInvalid(r, CalcReason::ForceNotFinite);
        TEST_ASSERT_FALSE(r.versions_known);
    }
}

// With both a force problem and a geometry problem, the force one is reported
// (documented check order). This is the firmware's state before #35 exists.
static void test_force_problem_is_reported_before_geometry_problem() {
    const MechanicalSetup unconfirmed = {0.0, false, false};
    assertBothInvalid(computeTorquePower(ForceMeasurement{}, rpm(600.0), unconfirmed, kLimits, 1000),
                      CalcReason::ForceUnavailable);
}

// ---------------------------------------------------------------------------
// Freshness. Fresh means age < limit (same rule as EcuState::isFresh), so an
// age equal to the limit is already stale.

static void assertTorqueValid25(const TorquePowerResult& r) {
    TEST_ASSERT_TRUE(r.torque.valid);
    TEST_ASSERT_EQUAL(CalcReason::None, r.torque.reason);
    ASSERT_CLOSE(25.0, r.torque.torque_nm);
}

// Force and RPM both at 1000 ms (no skew); RPM stays well inside its 2000 ms
// limit, so only the force's age decides.
static TorquePowerResult forceAgedTo(uint32_t now_ms) {
    return computeTorquePower(force(100.0, 1000), rpm(600.0, 1000), kFixtureSetup, kLimits, now_ms);
}

static void test_force_just_inside_freshness_is_used() {
    const TorquePowerResult r = forceAgedTo(1499); // age 499 ms < 500
    assertTorqueValid25(r);
    TEST_ASSERT_TRUE(r.power.valid);
}

static void test_force_at_freshness_limit_is_stale() {
    assertBothInvalid(forceAgedTo(1500), CalcReason::ForceStale); // age 500 ms
}

// A timestamp newer than now (now taken before the mailbox peek) looks ~49
// days old after unsigned subtraction: stale, never a negative age.
static void test_force_from_the_future_is_stale() {
    assertBothInvalid(forceAgedTo(999), CalcReason::ForceStale);
}

// millis() wraps at 2^32 ms (~49.7 days). Force read 256 ms before the wrap,
// now 16 ms after it: age 272 ms, still fresh.
static void test_force_age_survives_millis_rollover() {
    const TorquePowerResult r =
        computeTorquePower(force(100.0, 0xFFFFFF00u), rpm(600.0, 0x10u), kFixtureSetup, kLimits, 0x10u);
    assertTorqueValid25(r);
}

static void test_force_stale_across_millis_rollover() {
    // Read 256 ms before the wrap, now 244 ms after it: age 500 ms, stale.
    assertBothInvalid(computeTorquePower(force(100.0, 0xFFFFFF00u), rpm(600.0, 0xF4u), kFixtureSetup, kLimits, 0xF4u),
                      CalcReason::ForceStale);
}

// For RPM freshness, use a wide skew limit so only the RPM age matters.
static const CalcLimits kWideSkew = {500, 2000, 10000, 2.0};

static TorquePowerResult rpmAgedTo(uint32_t now_ms) {
    return computeTorquePower(force(100.0, now_ms), rpm(600.0, 1000), kFixtureSetup, kWideSkew, now_ms);
}

static void test_rpm_just_inside_freshness_is_used() {
    const TorquePowerResult r = rpmAgedTo(2999); // age 1999 ms < 2000
    TEST_ASSERT_TRUE(r.power.valid);
    ASSERT_CLOSE(1570.796327, r.power.power_w);
}

static void test_stale_rpm_gives_valid_torque_and_invalid_power() {
    const TorquePowerResult r = rpmAgedTo(3000); // age 2000 ms
    assertTorqueValid25(r);
    assertPowerInvalid(r, CalcReason::RpmStale);
}

static void test_rpm_from_the_future_is_stale() {
    const TorquePowerResult r = rpmAgedTo(999);
    assertPowerInvalid(r, CalcReason::RpmStale);
}

static void test_rpm_age_survives_millis_rollover() {
    // RPM 256 ms before the wrap, now (and force) 16 ms after: age 272 ms.
    const TorquePowerResult r =
        computeTorquePower(force(100.0, 0x10u), rpm(600.0, 0xFFFFFF00u), kFixtureSetup, kWideSkew, 0x10u);
    TEST_ASSERT_TRUE(r.power.valid);
}

// ---------------------------------------------------------------------------
// Source skew: force and RPM are the latest values, not a simultaneous
// measurement. Their timestamps may be at most 250 ms apart (inclusive).

static TorquePowerResult skewed(uint32_t force_ms, uint32_t rpm_ms, uint32_t now_ms) {
    return computeTorquePower(force(100.0, force_ms), rpm(600.0, rpm_ms), kFixtureSetup, kLimits, now_ms);
}

static void test_rpm_older_than_force_by_skew_limit_is_accepted() {
    const TorquePowerResult r = skewed(1250, 1000, 1250); // 250 ms apart
    TEST_ASSERT_TRUE(r.power.valid);
    ASSERT_CLOSE(1570.796327, r.power.power_w);
}

static void test_rpm_older_than_force_beyond_skew_limit_is_rejected() {
    const TorquePowerResult r = skewed(1251, 1000, 1251); // 251 ms apart; both still fresh
    assertTorqueValid25(r);
    assertPowerInvalid(r, CalcReason::SourceSkewTooLarge);
}

static void test_force_older_than_rpm_by_skew_limit_is_accepted() {
    TEST_ASSERT_TRUE(skewed(1000, 1250, 1250).power.valid);
}

static void test_force_older_than_rpm_beyond_skew_limit_is_rejected() {
    const TorquePowerResult r = skewed(1000, 1251, 1251);
    assertTorqueValid25(r);
    assertPowerInvalid(r, CalcReason::SourceSkewTooLarge);
}

// RPM just before the millis() wrap, force just after.
static void test_skew_across_millis_rollover_at_limit_is_accepted() {
    // 0x10 - 0xFFFFFF16 wraps to 0xFA = 250 ms.
    TEST_ASSERT_TRUE(skewed(0x10u, 0xFFFFFF16u, 0x10u).power.valid);
}

static void test_skew_across_millis_rollover_beyond_limit_is_rejected() {
    // 0x10 - 0xFFFFFF15 wraps to 0xFB = 251 ms.
    assertPowerInvalid(skewed(0x10u, 0xFFFFFF15u, 0x10u), CalcReason::SourceSkewTooLarge);
}

static void test_force_before_rollover_rpm_after_is_measured_both_ways() {
    // Force 0xFFFFFF16, RPM 0x10: 250 ms apart the other way round.
    TEST_ASSERT_TRUE(skewed(0xFFFFFF16u, 0x10u, 0x10u).power.valid);
    assertPowerInvalid(skewed(0xFFFFFF15u, 0x10u, 0x10u), CalcReason::SourceSkewTooLarge);
}

// ---------------------------------------------------------------------------
// Shaft-speed relationship and bad RPM values

// Configuration problem first, before anything about the RPM sample itself.
static void test_unconfirmed_shaft_is_reported_before_missing_rpm() {
    const MechanicalSetup setup = {0.25, true, false};
    const TorquePowerResult r = computeTorquePower(force(100.0), ShaftSpeedSample{}, setup, kLimits, 1000);
    assertPowerInvalid(r, CalcReason::ShaftSpeedUnconfirmed);
}

// NaN, infinite or negative RPM. The ECU's engine speed is unsigned, so a
// negative shaft speed is not a supported operating state.
static void test_invalid_rpm_is_rejected() {
    const double values[] = {NAN, INFINITY, -600.0};
    for (double v : values) {
        const TorquePowerResult r = computeTorquePower(force(100.0), rpm(v), kFixtureSetup, kLimits, 1000);
        assertTorqueValid25(r);
        assertPowerInvalid(r, CalcReason::RpmInvalid);
    }
}

// A bad value is reported before its age.
static void test_invalid_rpm_is_reported_before_stale_rpm() {
    assertPowerInvalid(computeTorquePower(force(100.0, 5000), rpm(NAN, 1000), kFixtureSetup, kLimits, 5000),
                       CalcReason::RpmInvalid);
}

// ---------------------------------------------------------------------------
// Result metadata: source timestamps and the calibration/tare version pair.

static void test_result_carries_source_timestamps() {
    const TorquePowerResult r = computeTorquePower(force(100.0, 1200), rpm(600.0, 1100), kFixtureSetup, kLimits, 1300);
    TEST_ASSERT_EQUAL_UINT32(1200, r.force_timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(1100, r.rpm_timestamp_ms);
}

// Issue table: "Calibration revision changes -> no mixed-revision result".
// Each result is labelled with the pair its own force was computed under.
static void test_calibration_change_never_mixes_revisions() {
    ForceMeasurement before = force(100.0, 1000);
    before.calibration_version = 1;
    before.tare_version = 1;
    ForceMeasurement after = force(50.0, 1100);
    after.calibration_version = 2;
    after.tare_version = 2;

    const TorquePowerResult r1 = computeTorquePower(before, rpm(600.0, 1000), kFixtureSetup, kLimits, 1000);
    const TorquePowerResult r2 = computeTorquePower(after, rpm(600.0, 1100), kFixtureSetup, kLimits, 1100);

    ASSERT_CLOSE(25.0, r1.torque.torque_nm);
    TEST_ASSERT_EQUAL_UINT32(1, r1.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(1, r1.tare_version);
    ASSERT_CLOSE(12.5, r2.torque.torque_nm); // 50 N x 0.25 m
    TEST_ASSERT_EQUAL_UINT32(2, r2.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, r2.tare_version);
}

// A tare advances only tare_version (#35): the pair is still copied as-is.
static void test_tare_only_change_keeps_calibration_version() {
    ForceMeasurement f = force(100.0);
    f.calibration_version = 4;
    f.tare_version = 9;
    const TorquePowerResult r = computeTorquePower(f, rpm(600.0), kFixtureSetup, kLimits, 1000);
    TEST_ASSERT_EQUAL_UINT32(4, r.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(9, r.tare_version);
}

// ---------------------------------------------------------------------------
// Seam C: engineSpeedFromEcu, fed through #33's real decoder.

static void decode(EcuState& ecu, uint32_t id, const uint8_t (&bytes)[8], uint32_t rx_ms) {
    CanFrame frame;
    frame.id = id;
    frame.dlc = 8;
    frame.data = bytes;
    frame.rx_ms = rx_ms;
    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(frame, ecu));
}

// #33 example: 0x520 bytes 70 17 .. E8 03 = 6000 rpm, lambda 1.000.
static const uint8_t kEngine6000[8] = {0x70, 0x17, 0, 0, 0, 0, 0xE8, 0x03};

static void test_engine_speed_never_received_is_missing() {
    const ShaftSpeedSample s = engineSpeedFromEcu(EcuState{});
    TEST_ASSERT_FALSE(s.received);
}

static void test_engine_speed_from_decoded_frame() {
    EcuState ecu;
    decode(ecu, 0x520, kEngine6000, 1234);
    const ShaftSpeedSample s = engineSpeedFromEcu(ecu);
    TEST_ASSERT_TRUE(s.received);
    TEST_ASSERT_EQUAL_DOUBLE(6000.0, s.rpm);
    TEST_ASSERT_EQUAL_UINT32(1234, s.timestamp_ms);
}

// #33 example: 60 EA = 60000 rpm (above int16 range, must stay positive).
static void test_engine_speed_above_32767_stays_positive() {
    EcuState ecu;
    const uint8_t bytes[8] = {0x60, 0xEA, 0, 0, 0, 0, 0, 0};
    decode(ecu, 0x520, bytes, 10);
    TEST_ASSERT_EQUAL_DOUBLE(60000.0, engineSpeedFromEcu(ecu).rpm);
}

// Other ECU messages arriving doesn't make RPM "received".
static void test_other_messages_do_not_count_as_engine_speed() {
    EcuState ecu;
    const uint8_t lambda[8] = {0xE8, 0x03, 0xE8, 0x03, 0, 0, 0, 0};
    decode(ecu, 0x521, lambda, 500);
    TEST_ASSERT_FALSE(engineSpeedFromEcu(ecu).received);
}

static void test_newer_engine_frame_moves_timestamp_forward() {
    EcuState ecu;
    decode(ecu, 0x520, kEngine6000, 1000);
    const uint8_t bytes[8] = {0x58, 0x02, 0, 0, 0, 0, 0, 0}; // 0x0258 = 600 rpm
    decode(ecu, 0x520, bytes, 1100);
    const ShaftSpeedSample s = engineSpeedFromEcu(ecu);
    TEST_ASSERT_EQUAL_DOUBLE(600.0, s.rpm);
    TEST_ASSERT_EQUAL_UINT32(1100, s.timestamp_ms);
}

// Whole path: decoded 6000 rpm, 100 N, fixture arm -> 15707.963268 W, 21.064726 hp.
static void test_decoded_rpm_gives_worked_power() {
    EcuState ecu;
    decode(ecu, 0x520, kEngine6000, 1000);
    const TorquePowerResult r =
        computeTorquePower(force(100.0, 1050), engineSpeedFromEcu(ecu), kFixtureSetup, kLimits, 1100);
    TEST_ASSERT_TRUE(r.power.valid);
    ASSERT_CLOSE(15707.963268, r.power.power_w);
    ASSERT_CLOSE(21.064726, r.power.horsepower_hp);
    TEST_ASSERT_EQUAL_UINT32(1000, r.rpm_timestamp_ms);
}

// ---------------------------------------------------------------------------
// Seam D: the example dataset handed to #38. Every row must hold.

static void assertCloseOrNan(double expected, double actual, const char* name) {
    if (isnan(expected)) {
        TEST_ASSERT_TRUE_MESSAGE(isnan(actual), name);
    } else {
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(fmax(1e-9, fabs(expected) * 1e-6), expected, actual, name);
    }
}

static void test_every_example_row_gives_its_expected_result() {
    TEST_ASSERT_GREATER_OR_EQUAL(14, kTorquePowerExampleCount);
    for (size_t i = 0; i < kTorquePowerExampleCount; ++i) {
        const TorquePowerExample& ex = kTorquePowerExamples[i];
        const TorquePowerResult r = computeTorquePower(ex.force, ex.speed, ex.setup, kExampleLimits, ex.now_ms);

        TEST_ASSERT_EQUAL_MESSAGE(ex.torque_valid, r.torque.valid, ex.name);
        TEST_ASSERT_EQUAL_MESSAGE(ex.torque_reason, r.torque.reason, ex.name);
        assertCloseOrNan(ex.torque_nm, r.torque.torque_nm, ex.name);
        TEST_ASSERT_EQUAL_MESSAGE(ex.power_valid, r.power.valid, ex.name);
        TEST_ASSERT_EQUAL_MESSAGE(ex.power_reason, r.power.reason, ex.name);
        assertCloseOrNan(ex.power_w, r.power.power_w, ex.name);
        assertCloseOrNan(ex.power_kw, r.power.power_kw, ex.name);
        assertCloseOrNan(ex.horsepower_hp, r.power.horsepower_hp, ex.name);
        TEST_ASSERT_EQUAL_MESSAGE(ex.reverse_torque, r.reverse_torque, ex.name);
        TEST_ASSERT_EQUAL_MESSAGE(ex.versions_known, r.versions_known, ex.name);
        if (ex.versions_known) {
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(ex.calibration_version, r.calibration_version, ex.name);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(ex.tare_version, r.tare_version, ex.name);
        }
    }
}

// The dataset's limits are the agreed DynoConfig.h values, so #38's fixture
// behaves like the firmware (geometry aside, which is a labelled fixture).
static void test_example_limits_are_the_agreed_values() {
    TEST_ASSERT_EQUAL_UINT32(500, kExampleLimits.force_freshness_ms);
    TEST_ASSERT_EQUAL_UINT32(2000, kExampleLimits.rpm_freshness_ms);
    TEST_ASSERT_EQUAL_UINT32(250, kExampleLimits.max_source_skew_ms);
    TEST_ASSERT_EQUAL_DOUBLE(2.0, kExampleLimits.max_arm_m);
}

// ---------------------------------------------------------------------------
// Reason names, for diagnostics lines (monitor now, #38 later).

static void test_reason_names_are_readable_and_distinct() {
    TEST_ASSERT_EQUAL_STRING("None", calcReasonName(CalcReason::None));
    TEST_ASSERT_EQUAL_STRING("ForceUnavailable", calcReasonName(CalcReason::ForceUnavailable));
    TEST_ASSERT_EQUAL_STRING("GeometryUnconfirmed", calcReasonName(CalcReason::GeometryUnconfirmed));
    TEST_ASSERT_EQUAL_STRING("SourceSkewTooLarge", calcReasonName(CalcReason::SourceSkewTooLarge));
    const size_t count = static_cast<size_t>(CalcReason::Count);
    TEST_ASSERT_EQUAL(12, count); // None + 11 reasons (catches a new reason without a name)
    for (size_t i = 0; i < count; ++i) {
        const char* a = calcReasonName(static_cast<CalcReason>(i));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(a, "?"));
        for (size_t j = i + 1; j < count; ++j) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(a, calcReasonName(static_cast<CalcReason>(j))));
        }
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reason_names_are_readable_and_distinct);
    RUN_TEST(test_every_example_row_gives_its_expected_result);
    RUN_TEST(test_example_limits_are_the_agreed_values);
    RUN_TEST(test_engine_speed_never_received_is_missing);
    RUN_TEST(test_engine_speed_from_decoded_frame);
    RUN_TEST(test_engine_speed_above_32767_stays_positive);
    RUN_TEST(test_other_messages_do_not_count_as_engine_speed);
    RUN_TEST(test_newer_engine_frame_moves_timestamp_forward);
    RUN_TEST(test_decoded_rpm_gives_worked_power);
    RUN_TEST(test_result_carries_source_timestamps);
    RUN_TEST(test_calibration_change_never_mixes_revisions);
    RUN_TEST(test_tare_only_change_keeps_calibration_version);
    RUN_TEST(test_unconfirmed_shaft_is_reported_before_missing_rpm);
    RUN_TEST(test_invalid_rpm_is_rejected);
    RUN_TEST(test_invalid_rpm_is_reported_before_stale_rpm);
    RUN_TEST(test_rpm_older_than_force_by_skew_limit_is_accepted);
    RUN_TEST(test_rpm_older_than_force_beyond_skew_limit_is_rejected);
    RUN_TEST(test_force_older_than_rpm_by_skew_limit_is_accepted);
    RUN_TEST(test_force_older_than_rpm_beyond_skew_limit_is_rejected);
    RUN_TEST(test_skew_across_millis_rollover_at_limit_is_accepted);
    RUN_TEST(test_skew_across_millis_rollover_beyond_limit_is_rejected);
    RUN_TEST(test_force_before_rollover_rpm_after_is_measured_both_ways);
    RUN_TEST(test_force_just_inside_freshness_is_used);
    RUN_TEST(test_force_at_freshness_limit_is_stale);
    RUN_TEST(test_force_from_the_future_is_stale);
    RUN_TEST(test_force_age_survives_millis_rollover);
    RUN_TEST(test_force_stale_across_millis_rollover);
    RUN_TEST(test_rpm_just_inside_freshness_is_used);
    RUN_TEST(test_stale_rpm_gives_valid_torque_and_invalid_power);
    RUN_TEST(test_rpm_from_the_future_is_stale);
    RUN_TEST(test_rpm_age_survives_millis_rollover);
    RUN_TEST(test_default_force_is_unavailable_not_zero);
    RUN_TEST(test_non_finite_force_is_rejected);
    RUN_TEST(test_force_problem_is_reported_before_geometry_problem);
    RUN_TEST(test_250_millimetres_is_a_quarter_metre);
    RUN_TEST(test_arm_from_millimetres_gives_the_reference_torque);
    RUN_TEST(test_arm_at_the_2_metre_limit_is_accepted);
    RUN_TEST(test_arm_just_over_the_2_metre_limit_is_rejected);
    RUN_TEST(test_impossible_arms_are_rejected);
    RUN_TEST(test_unconfirmed_geometry_gives_no_torque_or_power);
    RUN_TEST(test_helpers_match_the_issue_reference_calculation);
    return UNITY_END();
}
