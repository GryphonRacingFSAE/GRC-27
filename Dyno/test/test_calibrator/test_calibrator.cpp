// LoadCellCalibrator (#35 step 4): the calibration / tare procedure, the
// version counters and storage, driven with fake samples, a fake clock and
// fake storage (no HX711, no flash, no serial).
#include <math.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "CalibrationExamples.h"
#include "FakeCalibrationStore.h"
#include "FakeCommands.h"
#include "LoadCellCalibrator.h"

void setUp() {}
void tearDown() {}

// 1 s window, >= 8 samples, spread <= 500, 10 s timeout, saturated at
// 8388000; 5000-count minimum separation.
static const CalibratorConfig kConfig = kExampleCalibratorConfig;

static LoadCellSample valid(int32_t raw, uint32_t t) {
    LoadCellSample s;
    s.status = LoadCellStatus::Valid;
    s.raw = raw;
    s.timestamp_ms = t;
    return s;
}

// Sends a typed command (through the real parser).
static void type(LoadCellCalibrator& cal, const char* line, uint32_t now_ms) {
    const ParseResult p = parseCommand(line);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("None", parseErrorName(p.error), line);
    cal.command(p.command, now_ms);
}

// Steps with a new sample of `raw` every `period` ms for t = from, from+period,
// ... while t <= to. Returns the last force; `ready_seen` is set if any step
// published calibration_ready.
static ForceMeasurement feed(LoadCellCalibrator& cal, uint32_t from, uint32_t to, uint32_t period, int32_t raw,
                             bool* ready_seen = nullptr) {
    ForceMeasurement f;
    for (uint32_t t = from; t <= to; t += period) {
        f = cal.step(valid(raw, t), t);
        if (ready_seen && f.calibration_ready) *ready_seen = true;
    }
    return f;
}

// The issue's worked example as a whole procedure, from an empty store.
// Returns with the calibration accepted at t = 6000.
static void calibrateWorkedExample(LoadCellCalibrator& cal) {
    type(cal, "cal unloaded", 0);
    feed(cal, 0, 1000, 100, 100000); // stable at 1000: mean 100000
    type(cal, "cal loaded 100N", 5000);
    feed(cal, 5000, 6000, 100, 120000); // stable at 6000: mean 120000
}

// ---------------------------------------------------------------------------

// Nothing stored: not calibrated, and a reading gives no force at all, not a
// plausible 0 N.
static void test_boot_with_nothing_stored_is_not_calibrated() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();

    TEST_ASSERT_TRUE(cal.bootStatus() == StoreLoad::Missing);
    TEST_ASSERT_FALSE(cal.calibrated());
    TEST_ASSERT_TRUE(replies.any("WARNING: no calibration stored"));

    const ForceMeasurement f = cal.step(valid(100000, 50), 50);
    TEST_ASSERT_FALSE(f.available);
    TEST_ASSERT_TRUE(isnan(f.force_n));
    TEST_ASSERT_FALSE(f.calibration_ready);
}

// unloaded 100000, loaded 120000 under 100 N -> 200 counts/N, offset 100000;
// 110000 then reads 50 N. First calibration: both counters go 0 -> 1.
static void test_worked_example_calibration_is_accepted_and_saved() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    TEST_ASSERT_TRUE(cal.calibrated());
    const CalibrationRecord r = cal.active();
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, r.offset_counts);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, r.counts_per_n);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, r.reference_force_n);
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, r.unloaded_counts);
    TEST_ASSERT_EQUAL_DOUBLE(120000.0, r.loaded_counts);
    TEST_ASSERT_TRUE(isnan(r.reference_mass_kg));
    TEST_ASSERT_TRUE(isnan(r.gravity_m_s2));
    TEST_ASSERT_EQUAL_UINT32(1, r.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(1, r.tare_version);
    TEST_ASSERT_EQUAL_UINT32(1, store.writes);
    TEST_ASSERT_EQUAL_UINT32(76, store.bytes.size());
    TEST_ASSERT_TRUE(replies.any("cal OK"));

    const ForceMeasurement f = cal.step(valid(110000, 6100), 6100);
    TEST_ASSERT_TRUE(f.available);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, f.force_n);
    TEST_ASSERT_EQUAL_UINT32(6100, f.timestamp_ms);
    TEST_ASSERT_TRUE(f.calibration_ready);
    TEST_ASSERT_EQUAL_UINT32(1, f.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(1, f.tare_version);
}

// A calibrated rig, rebooted from `store` (which must hold the worked example).
static void assertReadsWorkedExample(LoadCellCalibrator& cal, uint32_t t) {
    const ForceMeasurement f = cal.step(valid(110000, t), t);
    TEST_ASSERT_TRUE(f.available);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, f.force_n);
    TEST_ASSERT_TRUE(f.calibration_ready);
    TEST_ASSERT_EQUAL_UINT32(1, f.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(1, f.tare_version);
}

// From "cal unloaded" until the procedure ends, every published measurement
// has calibration_ready = 0 and no force, even though an older calibration
// exists, including the wait while the operator hangs the load.
static void test_not_ready_for_the_whole_procedure() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    assertReadsWorkedExample(cal, 6100);

    bool ready_seen = false;
    type(cal, "cal unloaded", 7000);
    ForceMeasurement f = feed(cal, 7000, 8000, 100, 100000, &ready_seen);
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::AwaitingLoad);
    f = feed(cal, 8100, 12000, 100, 110000, &ready_seen); // hanging the weight
    TEST_ASSERT_FALSE(f.available);
    TEST_ASSERT_TRUE(isnan(f.force_n));
    type(cal, "cal loaded 100N", 12000);
    feed(cal, 12000, 12900, 100, 120000, &ready_seen); // not yet stable
    TEST_ASSERT_FALSE(ready_seen);

    f = cal.step(valid(120000, 13000), 13000); // stable: accepted
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::Idle);
    f = cal.step(valid(110000, 13100), 13100);
    TEST_ASSERT_TRUE(f.calibration_ready);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, f.force_n);
    TEST_ASSERT_EQUAL_UINT32(2, f.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, f.tare_version);
}

// A new calibrator on the same storage is a reboot: same record, same pair.
static void test_reboot_restores_the_saved_calibration() {
    FakeCalibrationStore store;
    {
        FakeReplies replies;
        LoadCellCalibrator cal(store, replies, kConfig);
        cal.begin();
        calibrateWorkedExample(cal);
    }
    FakeReplies replies;
    LoadCellCalibrator rebooted(store, replies, kConfig);
    rebooted.begin();
    TEST_ASSERT_TRUE(rebooted.bootStatus() == StoreLoad::Ok);
    TEST_ASSERT_FALSE(replies.any("WARNING"));
    TEST_ASSERT_TRUE(replies.any("cal: loaded the saved calibration"));
    const CalibrationRecord r = rebooted.active();
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, r.offset_counts);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, r.counts_per_n);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, r.reference_force_n);
    TEST_ASSERT_EQUAL_DOUBLE(120000.0, r.loaded_counts);
    assertReadsWorkedExample(rebooted, 100);
}

// Recalibrating the other way round (loaded 80000): -200 counts/N, both
// counters advance once more, and 90000 then reads +50 N.
static void test_recalibration_advances_both_counters() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 8000, 100, 100000);
    type(cal, "cal loaded 100N", 9000);
    feed(cal, 9000, 10000, 100, 80000);
    TEST_ASSERT_EQUAL_DOUBLE(-200.0, cal.active().counts_per_n);
    const ForceMeasurement f = cal.step(valid(90000, 10100), 10100);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, f.force_n);
    TEST_ASSERT_EQUAL_UINT32(2, f.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, f.tare_version);
    TEST_ASSERT_EQUAL_UINT32(2, store.writes);
}

// Runs a second, different calibration (scale -200) against `store`, whose
// writes are about to go wrong. It must be rejected: the worked example stays
// active and is what a reboot finds.
static void assertFailedSaveKeepsWorkedExample(FakeCalibrationStore& store, LoadCellCalibrator& cal,
                                               FakeReplies& replies) {
    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 8000, 100, 100000);
    type(cal, "cal loaded 100N", 9000);
    feed(cal, 9000, 10000, 100, 80000);

    TEST_ASSERT_TRUE(replies.any("cal FAILED: could not save"));
    TEST_ASSERT_TRUE(replies.any("previous calibration kept"));
    TEST_ASSERT_FALSE(replies.any("cal OK: offset=100000.0 counts scale=-200"));
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::Idle);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, cal.active().counts_per_n);
    assertReadsWorkedExample(cal, 10100);

    store.fail_writes = false;
    FakeReplies boot_replies;
    LoadCellCalibrator rebooted(store, boot_replies, kConfig);
    rebooted.begin();
    TEST_ASSERT_EQUAL_STRING("Ok", storeLoadName(rebooted.bootStatus()));
    assertReadsWorkedExample(rebooted, 100);
}

static void test_a_write_that_fails_rejects_the_calibration() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    store.fail_writes = true;
    assertFailedSaveKeepsWorkedExample(store, cal, replies);
}

// The write "succeeds" but the bytes read back differ (bad flash): also
// rejected, and the previous record is written back so that storage still
// matches what is active. Writes: 1 first calibration, 2 the garbled new
// record, 3 the previous record again.
static void test_a_write_that_reads_back_wrong_rejects_the_calibration() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    store.garble_next_write = true;
    assertFailedSaveKeepsWorkedExample(store, cal, replies);
    TEST_ASSERT_EQUAL_UINT32(3, store.writes);
}

// First-ever calibration fails to save: still not calibrated, and storage
// holds nothing usable.
static void test_a_failed_first_save_leaves_the_rig_uncalibrated() {
    FakeCalibrationStore store;
    store.fail_writes = true;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    TEST_ASSERT_FALSE(cal.calibrated());
    TEST_ASSERT_TRUE(replies.any("cal FAILED: could not save"));
    TEST_ASSERT_TRUE(replies.any("still not calibrated"));
    const ForceMeasurement f = cal.step(valid(110000, 6100), 6100);
    TEST_ASSERT_FALSE(f.available);
    TEST_ASSERT_FALSE(f.calibration_ready);
}

// After a worked-example calibration, a rejected attempt leaves everything
// as it was: record, counters, storage (no extra write) and readiness.
static void assertUnchangedAfterRejection(FakeCalibrationStore& store, LoadCellCalibrator& cal, uint32_t t) {
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::Idle);
    TEST_ASSERT_EQUAL_UINT32(1, store.writes);
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, cal.active().offset_counts);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, cal.active().counts_per_n);
    assertReadsWorkedExample(cal, t);
}

// loaded - unloaded = 4999 counts < 5000: too close to tell from noise.
static void test_too_little_separation_is_rejected() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 8000, 100, 100000);
    type(cal, "cal loaded 100N", 9000);
    feed(cal, 9000, 10000, 100, 104999);
    TEST_ASSERT_TRUE(replies.any("cal FAILED: calibration rejected: SeparationTooSmall; previous calibration kept"));
    assertUnchangedAfterRejection(store, cal, 10100);
}

// The loaded reading swings by 1000 counts and never settles: fails at the
// 10 s timeout, counted from "cal loaded".
static void test_an_unstable_loaded_reading_times_out() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 8000, 100, 100000);
    type(cal, "cal loaded 100N", 9000);
    for (uint32_t t = 9000; t < 19000; t += 100) {
        cal.step(valid((t / 100) % 2 ? 121000 : 120000, t), t);
    }
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::CollectingLoaded); // 9.9 s in: still trying
    cal.step(valid(120000, 19000), 19000);
    TEST_ASSERT_TRUE(replies.any("cal FAILED: reading not stable: Unstable"));
    assertUnchangedAfterRejection(store, cal, 19100);
}

// A failed unloaded reading ends the procedure too; "cal loaded" then has
// nothing to pair with.
static void test_a_saturated_unloaded_reading_ends_the_procedure() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 7300, 100, 100000);
    cal.step(valid(8388607, 7400), 7400);
    TEST_ASSERT_TRUE(replies.any("cal FAILED: reading not stable: Saturated"));
    assertUnchangedAfterRejection(store, cal, 7500);
    type(cal, "cal loaded 100N", 7600);
    TEST_ASSERT_TRUE(replies.any("cal loaded: type \"cal unloaded\" first"));
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::Idle);
}

static void test_a_sensor_fault_while_collecting_fails_at_once() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 7500, 100, 100000);
    LoadCellSample fault;
    fault.status = LoadCellStatus::Fault;
    fault.timestamp_ms = 7500;
    cal.step(fault, 7600);
    TEST_ASSERT_TRUE(replies.any("cal FAILED: reading not stable: SensorFault"));
    assertUnchangedAfterRejection(store, cal, 7700);
}

// Commands at the wrong moment are refused with a reply and change nothing.
static void test_commands_out_of_order_are_refused() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();

    type(cal, "cal loaded 100N", 0); // no unloaded reading yet
    TEST_ASSERT_TRUE(replies.any("cal loaded: type \"cal unloaded\" first"));
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::Idle);

    type(cal, "cal unloaded", 100);
    type(cal, "cal loaded 100N", 200); // unloaded still collecting
    TEST_ASSERT_TRUE(replies.any("cal loaded: still collecting"));
    type(cal, "cal unloaded", 300); // already running
    TEST_ASSERT_TRUE(replies.any("cal unloaded: busy"));
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::CollectingUnloaded);

    // The original collection carries on unaffected (started at 100).
    feed(cal, 100, 1100, 100, 100000);
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::AwaitingLoad);
}

// "cal cancel" abandons the procedure at any point; the previous
// calibration is back in use straight away.
static void test_cancel_abandons_the_procedure() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    type(cal, "cal unloaded", 7000);
    feed(cal, 7000, 8000, 100, 100000);
    type(cal, "cal cancel", 8050);
    TEST_ASSERT_TRUE(replies.any("cal: cancelled; previous calibration kept"));
    assertUnchangedAfterRejection(store, cal, 8100);

    type(cal, "cal unloaded", 9000);
    feed(cal, 9000, 10000, 100, 100000);
    type(cal, "cal loaded 100N", 11000);
    feed(cal, 11000, 11500, 100, 80000);
    type(cal, "cal cancel", 11550);
    replies.lines.clear();
    LoadCellSample fault;
    fault.status = LoadCellStatus::Fault;
    cal.step(fault, 11560);              // would have failed it
    feed(cal, 11600, 13000, 100, 80000); // would have been stable at 12000
    TEST_ASSERT_EQUAL_UINT32(0, replies.lines.size()); // a cancelled collection says nothing more
    assertUnchangedAfterRejection(store, cal, 13100);

    replies.lines.clear();
    type(cal, "cal cancel", 14000);
    TEST_ASSERT_TRUE(replies.any("cal cancel: nothing to cancel"));
}

// A sensor fault (or no sample yet) on a calibrated rig: no force, but the
// calibration itself is still fine, so calibration_ready stays 1.
static void test_no_force_without_a_valid_sample() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    LoadCellSample fault;
    fault.status = LoadCellStatus::Fault;
    fault.raw = 110000; // a stale value must not be used
    fault.timestamp_ms = 6100;
    ForceMeasurement f = cal.step(fault, 6200);
    TEST_ASSERT_FALSE(f.available);
    TEST_ASSERT_TRUE(isnan(f.force_n));
    TEST_ASSERT_TRUE(f.calibration_ready);

    LoadCellSample none; // NoSampleYet
    none.raw = 110000;
    f = cal.step(none, 6300);
    TEST_ASSERT_FALSE(f.available);
    TEST_ASSERT_TRUE(isnan(f.force_n));
}

// ---------------------------------------------------------------------------
// Tare

// Tare moves only the zero: a stable 101000 becomes the new offset, the scale
// and calibration_version stay, tare_version goes 1 -> 2. 111000 then reads
// (111000 - 101000) / 200 = 50 N.
static void test_tare_replaces_only_the_offset() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    bool ready_seen = false;
    type(cal, "tare", 7000);
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::CollectingTare);
    feed(cal, 7000, 7900, 100, 101000, &ready_seen);
    TEST_ASSERT_FALSE(ready_seen); // not ready while the tare collects
    cal.step(valid(101000, 8000), 8000);
    TEST_ASSERT_TRUE(replies.any("tare OK"));

    const CalibrationRecord r = cal.active();
    TEST_ASSERT_EQUAL_DOUBLE(101000.0, r.offset_counts);
    TEST_ASSERT_EQUAL_DOUBLE(200.0, r.counts_per_n);
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, r.unloaded_counts); // the calibration's own reading stays on record
    TEST_ASSERT_EQUAL_UINT32(1, r.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, r.tare_version);

    const ForceMeasurement f = cal.step(valid(111000, 8100), 8100);
    TEST_ASSERT_TRUE(f.calibration_ready);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, f.force_n);
    TEST_ASSERT_EQUAL_UINT32(1, f.calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, f.tare_version);
}

// Tares are not saved (decided 2026-11-09): after a reboot the saved
// calibration's own offset and pair come back, so tare_version 2 can be
// reused in the next boot. Tare again after every power-up.
static void test_tare_is_not_saved_and_a_reboot_brings_back_the_calibration_zero() {
    FakeCalibrationStore store;
    {
        FakeReplies replies;
        LoadCellCalibrator cal(store, replies, kConfig);
        cal.begin();
        calibrateWorkedExample(cal);
        type(cal, "tare", 7000);
        feed(cal, 7000, 8000, 100, 101000);
        TEST_ASSERT_TRUE(replies.any("not saved"));
        TEST_ASSERT_EQUAL_UINT32(1, store.writes);
    }
    FakeReplies replies;
    LoadCellCalibrator rebooted(store, replies, kConfig);
    rebooted.begin();
    TEST_ASSERT_EQUAL_DOUBLE(100000.0, rebooted.active().offset_counts);
    assertReadsWorkedExample(rebooted, 100);
}

// With no calibration there is no scale for a zero to belong to.
static void test_tare_before_any_calibration_is_refused() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    type(cal, "tare", 0);
    TEST_ASSERT_TRUE(replies.any("tare: refused, not calibrated"));
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::Idle);
    TEST_ASSERT_EQUAL_UINT32(0, cal.active().tare_version);
}

static void test_a_failed_tare_keeps_the_old_zero() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);
    type(cal, "tare", 7000);
    for (uint32_t t = 7000; t <= 17000; t += 100) {
        cal.step(valid((t / 100) % 2 ? 101000 : 100000, t), t); // 1000-count wobble
    }
    TEST_ASSERT_TRUE(replies.any("tare FAILED: reading not stable: Unstable; previous zero kept"));
    assertUnchangedAfterRejection(store, cal, 17100);
}

// A tare can't start in the middle of a calibration, and vice versa.
static void test_tare_and_calibration_do_not_overlap() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    type(cal, "cal unloaded", 7000);
    type(cal, "tare", 7100);
    TEST_ASSERT_TRUE(replies.any("tare: busy"));
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::CollectingUnloaded);
    type(cal, "cal cancel", 7200);

    type(cal, "tare", 7300);
    type(cal, "cal unloaded", 7400);
    TEST_ASSERT_TRUE(replies.any("cal unloaded: busy"));
    type(cal, "cal loaded 100N", 7500);
    TEST_ASSERT_TRUE(cal.phase() == CalibratorPhase::CollectingTare);
    type(cal, "cal cancel", 7600);
    TEST_ASSERT_TRUE(replies.any("cancelled; previous calibration kept"));
    TEST_ASSERT_EQUAL_UINT32(1, cal.active().tare_version);
}

// ---------------------------------------------------------------------------
// Status

static void test_status_reports_the_active_calibration() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    type(cal, "cal status", 0);
    TEST_ASSERT_TRUE(replies.any("cal status: not calibrated (Missing)"));

    calibrateWorkedExample(cal);
    type(cal, "cal status", 6500);
    TEST_ASSERT_EQUAL_STRING("cal status: calibrated offset=100000.0 counts scale=200.0000 counts/N "
                             "reference=100.000 N calibration_version=1 tare_version=1; idle",
                             replies.last().c_str());

    type(cal, "cal unloaded", 7000);
    type(cal, "cal status", 7100);
    TEST_ASSERT_TRUE(replies.last().find("; collecting the unloaded reading") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Storage problems

// The worked example's record as it would be stored.
static std::vector<uint8_t> workedRecordBytes(uint32_t calibration_version = 1, uint32_t tare_version = 1) {
    CalibrationRecord r;
    r.valid = true;
    r.offset_counts = 100000.0;
    r.counts_per_n = 200.0;
    r.reference_force_n = 100.0;
    r.unloaded_counts = 100000.0;
    r.loaded_counts = 120000.0;
    r.calibration_version = calibration_version;
    r.tare_version = tare_version;
    std::vector<uint8_t> bytes(kCalibrationRecordBytes);
    encodeCalibration(r, bytes.data());
    return bytes;
}

// Re-seals bytes 0..71 with a fresh CRC, so only the change we made is wrong.
static void reseal(std::vector<uint8_t>& b) {
    const uint32_t crc = crc32(b.data(), 72);
    for (int i = 0; i < 4; ++i) b[72 + i] = static_cast<uint8_t>(crc >> (8 * i));
}

static void assertBootsUncalibrated(const std::vector<uint8_t>& bytes, const char* expected_status) {
    FakeCalibrationStore store;
    store.present = true;
    store.bytes = bytes;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    TEST_ASSERT_EQUAL_STRING(expected_status, storeLoadName(cal.bootStatus()));
    TEST_ASSERT_FALSE(cal.calibrated());
    const std::string warning = std::string("WARNING: no calibration stored (") + expected_status + ")";
    TEST_ASSERT_TRUE(replies.any(warning.c_str()));
    const ForceMeasurement f = cal.step(valid(110000, 50), 50);
    TEST_ASSERT_FALSE(f.available);
    TEST_ASSERT_FALSE(f.calibration_ready);
}

// The CRC-32 is the standard one: its published check value for "123456789".
static void test_crc32_check_value() {
    const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32(text, sizeof text));
}

static void test_an_undamaged_record_loads() {
    FakeCalibrationStore store;
    store.present = true;
    store.bytes = workedRecordBytes();
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    TEST_ASSERT_EQUAL_STRING("Ok", storeLoadName(cal.bootStatus()));
    assertReadsWorkedExample(cal, 50);
}

static void test_damaged_records_are_corrupt() {
    std::vector<uint8_t> b = workedRecordBytes();
    b[20] ^= 0x01; // one bit of the scale, CRC not updated
    assertBootsUncalibrated(b, "Corrupt");

    b = workedRecordBytes();
    b[75] ^= 0x80; // the CRC itself
    assertBootsUncalibrated(b, "Corrupt");

    b = workedRecordBytes();
    b[0] = 'X'; // not our magic, even with a matching CRC
    reseal(b);
    assertBootsUncalibrated(b, "Corrupt");

    b = workedRecordBytes();
    b.resize(40); // half written
    assertBootsUncalibrated(b, "Corrupt");

    b = workedRecordBytes();
    b.push_back(0); // too long
    assertBootsUncalibrated(b, "Corrupt");

    assertBootsUncalibrated(std::vector<uint8_t>(), "Corrupt"); // key there, no bytes

    // Cut short by one byte, where that byte was 0: the CRC still checks out
    // over the zero-filled buffer, so only the length gives it away. (Try
    // tare versions until the CRC's last byte happens to be 0.)
    uint32_t tv = 0;
    do {
        b = workedRecordBytes(1, ++tv);
    } while (b[75] != 0);
    b.resize(75);
    assertBootsUncalibrated(b, "Corrupt");

    b = workedRecordBytes();
    b[6] = 75; // length field wrong, CRC resealed
    reseal(b);
    assertBootsUncalibrated(b, "Corrupt");
}

// A sealed record whose numbers can't be used (scale 0, offset NaN) must not
// become a calibration, CRC or not.
static void test_sealed_but_unusable_numbers_are_corrupt() {
    std::vector<uint8_t> b = workedRecordBytes();
    for (int i = 16; i < 24; ++i) b[i] = 0; // counts_per_n = 0.0
    reseal(b);
    assertBootsUncalibrated(b, "Corrupt");

    b = workedRecordBytes();
    const uint8_t nan_le[8] = {0, 0, 0, 0, 0, 0, 0xF8, 0x7F}; // quiet NaN, little-endian
    for (int i = 0; i < 8; ++i) b[8 + i] = nan_le[i];          // offset_counts
    reseal(b);
    assertBootsUncalibrated(b, "Corrupt");
}

// A record written by a different layout (schema 2) is reported as such,
// whether or not it is the same size.
static void test_another_schema_is_unsupported() {
    std::vector<uint8_t> b = workedRecordBytes();
    b[4] = 2;
    reseal(b);
    assertBootsUncalibrated(b, "UnsupportedSchema");

    b.resize(20);
    assertBootsUncalibrated(b, "UnsupportedSchema");
}

// The bytes really are the documented layout: 100000.0 is 0x40F86A0000000000.
static void test_the_stored_layout_matches_the_documentation() {
    const std::vector<uint8_t> b = workedRecordBytes(7, 9);
    TEST_ASSERT_EQUAL_UINT8('D', b[0]);
    TEST_ASSERT_EQUAL_UINT8('L', b[3]);
    TEST_ASSERT_EQUAL_UINT8(1, b[4]);
    TEST_ASSERT_EQUAL_UINT8(0, b[5]);
    TEST_ASSERT_EQUAL_UINT8(76, b[6]);
    const uint8_t offset_le[8] = {0, 0, 0, 0, 0, 0x6A, 0xF8, 0x40};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(offset_le, &b[8], 8);
    TEST_ASSERT_EQUAL_UINT8(7, b[64]);
    TEST_ASSERT_EQUAL_UINT8(9, b[68]);
}

// ---------------------------------------------------------------------------
// Units, counters, coherence

// 10.2 kg -> 100.02783 N, with the mass and g on record and kept by a reboot.
static void test_a_calibration_in_kilograms_records_mass_and_gravity() {
    FakeCalibrationStore store;
    {
        FakeReplies replies;
        LoadCellCalibrator cal(store, replies, kConfig);
        cal.begin();
        type(cal, "cal unloaded", 0);
        feed(cal, 0, 1000, 100, 100000);
        type(cal, "cal loaded 10.2kg", 2000);
        feed(cal, 2000, 3000, 100, 120000);
        TEST_ASSERT_TRUE(cal.calibrated());
    }
    FakeReplies replies;
    LoadCellCalibrator rebooted(store, replies, kConfig);
    rebooted.begin();
    const CalibrationRecord r = rebooted.active();
    TEST_ASSERT_EQUAL_DOUBLE(10.2, r.reference_mass_kg);
    TEST_ASSERT_EQUAL_DOUBLE(9.80665, r.gravity_m_s2);
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.02783, r.reference_force_n);
    // The loaded point reads back as the reference force.
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 100.02783, rebooted.step(valid(120000, 50), 50).force_n);
}

// 32-bit counters wrap: after 4294967295 comes 0.
static void test_counters_wrap_around() {
    FakeCalibrationStore store;
    store.present = true;
    store.bytes = workedRecordBytes(0xFFFFFFFFu, 0xFFFFFFFEu);
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    type(cal, "tare", 0);
    feed(cal, 0, 1000, 100, 100000);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, cal.active().tare_version);
    type(cal, "cal unloaded", 2000);
    feed(cal, 2000, 3000, 100, 100000);
    type(cal, "cal loaded 100N", 4000);
    feed(cal, 4000, 5000, 100, 120000);
    TEST_ASSERT_EQUAL_UINT32(0, cal.active().calibration_version);
    TEST_ASSERT_EQUAL_UINT32(0, cal.active().tare_version);
}

// Through a recalibration and a tare, every published force matches exactly
// the settings its version pair names: never a new offset with an old scale.
static void test_every_force_matches_its_version_pair() {
    FakeCalibrationStore store;
    FakeReplies replies;
    LoadCellCalibrator cal(store, replies, kConfig);
    cal.begin();
    calibrateWorkedExample(cal);

    struct Settings {
        uint32_t cal_v, tare_v;
        double offset, scale;
    };
    const Settings known[] = {{1, 1, 100000.0, 200.0}, {2, 2, 100000.0, -200.0}, {2, 3, 98000.0, -200.0}};
    int checked = 0;
    int seen[3] = {0, 0, 0};
    auto check = [&](const ForceMeasurement& f, int32_t raw) {
        if (!f.calibration_ready) {
            TEST_ASSERT_FALSE(f.available);
            return;
        }
        for (int i = 0; i < 3; ++i) {
            if (f.calibration_version == known[i].cal_v && f.tare_version == known[i].tare_v) {
                TEST_ASSERT_EQUAL_DOUBLE((raw - known[i].offset) / known[i].scale, f.force_n);
                ++seen[i];
                ++checked;
                return;
            }
        }
        TEST_FAIL_MESSAGE("a version pair no accepted settings had");
    };
    // A fixed script: (time, raw, command or null).
    struct Step {
        uint32_t t;
        int32_t raw;
        const char* command;
    };
    std::vector<Step> script;
    for (uint32_t t = 6100; t <= 6900; t += 100) script.push_back({t, 110000, nullptr});
    script.push_back({7000, 100000, "cal unloaded"});
    for (uint32_t t = 7100; t <= 8000; t += 100) script.push_back({t, 100000, nullptr});
    script.push_back({9000, 80000, "cal loaded 100N"});
    for (uint32_t t = 9100; t <= 11000; t += 100) script.push_back({t, 80000, nullptr});
    script.push_back({11100, 98000, "tare"});
    for (uint32_t t = 11200; t <= 12500; t += 100) script.push_back({t, 98000, nullptr});
    for (uint32_t t = 12600; t <= 13000; t += 100) script.push_back({t, 88000, nullptr});
    for (const Step& st : script) {
        if (st.command) type(cal, st.command, st.t);
        check(cal.step(valid(st.raw, st.t), st.t), st.raw);
    }
    TEST_ASSERT_TRUE(seen[0] > 0 && seen[1] > 0 && seen[2] > 0);
    TEST_ASSERT_EQUAL_DOUBLE(60.0, cal.step(valid(86000, 13100), 13100).force_n); // (86000-98000)/-200
    (void)checked;
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_with_nothing_stored_is_not_calibrated);
    RUN_TEST(test_worked_example_calibration_is_accepted_and_saved);
    RUN_TEST(test_not_ready_for_the_whole_procedure);
    RUN_TEST(test_reboot_restores_the_saved_calibration);
    RUN_TEST(test_recalibration_advances_both_counters);
    RUN_TEST(test_a_write_that_fails_rejects_the_calibration);
    RUN_TEST(test_a_write_that_reads_back_wrong_rejects_the_calibration);
    RUN_TEST(test_a_failed_first_save_leaves_the_rig_uncalibrated);
    RUN_TEST(test_too_little_separation_is_rejected);
    RUN_TEST(test_an_unstable_loaded_reading_times_out);
    RUN_TEST(test_a_saturated_unloaded_reading_ends_the_procedure);
    RUN_TEST(test_a_sensor_fault_while_collecting_fails_at_once);
    RUN_TEST(test_commands_out_of_order_are_refused);
    RUN_TEST(test_cancel_abandons_the_procedure);
    RUN_TEST(test_no_force_without_a_valid_sample);
    RUN_TEST(test_tare_replaces_only_the_offset);
    RUN_TEST(test_tare_is_not_saved_and_a_reboot_brings_back_the_calibration_zero);
    RUN_TEST(test_tare_before_any_calibration_is_refused);
    RUN_TEST(test_a_failed_tare_keeps_the_old_zero);
    RUN_TEST(test_tare_and_calibration_do_not_overlap);
    RUN_TEST(test_status_reports_the_active_calibration);
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_an_undamaged_record_loads);
    RUN_TEST(test_damaged_records_are_corrupt);
    RUN_TEST(test_sealed_but_unusable_numbers_are_corrupt);
    RUN_TEST(test_another_schema_is_unsupported);
    RUN_TEST(test_the_stored_layout_matches_the_documentation);
    RUN_TEST(test_a_calibration_in_kilograms_records_mass_and_gravity);
    RUN_TEST(test_counters_wrap_around);
    RUN_TEST(test_every_force_matches_its_version_pair);
    return UNITY_END();
}
