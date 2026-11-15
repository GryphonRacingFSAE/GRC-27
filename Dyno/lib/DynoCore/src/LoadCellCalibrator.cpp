#include "LoadCellCalibrator.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

LoadCellCalibrator::LoadCellCalibrator(CalibrationStore& store, CalibrationReplies& replies,
                                       const CalibratorConfig& config)
    : store_(store), replies_(replies), config_(config), window_(config.window) {}

void LoadCellCalibrator::say(const char* fmt, ...) {
    char text[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    replies_.reply(text);
}

void LoadCellCalibrator::begin() {
    const DecodedCalibration d = loadCalibration(store_);
    boot_status_ = d.status;
    if (d.status == StoreLoad::Ok) {
        active_ = d.record;
        say("cal: loaded the saved calibration: offset=%.1f counts scale=%.4f counts/N calibration_version=%lu "
            "tare_version=%lu",
            active_.offset_counts, active_.counts_per_n, static_cast<unsigned long>(active_.calibration_version),
            static_cast<unsigned long>(active_.tare_version));
        return;
    }
    say("WARNING: no calibration stored (%s): force, torque and power are blank until calibrated",
        storeLoadName(d.status));
}

const char* LoadCellCalibrator::keptText() const {
    return calibrated() ? "previous calibration kept" : "still not calibrated";
}

void LoadCellCalibrator::command(const LoadCellCommand& command, uint32_t now_ms) {
    switch (command.kind) {
    case CommandKind::CalUnloaded:
        // Allowed when idle, or to redo the unloaded reading before "cal loaded".
        if (phase_ != CalibratorPhase::Idle && phase_ != CalibratorPhase::AwaitingLoad) {
            say("cal unloaded: busy collecting; type \"cal cancel\" first");
            return;
        }
        phase_ = CalibratorPhase::CollectingUnloaded;
        window_.start(now_ms);
        say("cal: collecting the unloaded reading; keep the arm still with nothing hanging on it");
        return;

    case CommandKind::CalLoaded:
        if (phase_ == CalibratorPhase::Idle) {
            say("cal loaded: type \"cal unloaded\" first");
            return;
        }
        if (phase_ != CalibratorPhase::AwaitingLoad) {
            say("cal loaded: still collecting; wait for its result or type \"cal cancel\"");
            return;
        }
        loaded_command_ = command;
        phase_ = CalibratorPhase::CollectingLoaded;
        window_.start(now_ms);
        say("cal: collecting the loaded reading (%.3f N); keep the load still", command.reference_force_n);
        return;

    case CommandKind::CalCancel:
        if (phase_ == CalibratorPhase::Idle) {
            say("cal cancel: nothing to cancel");
            return;
        }
        phase_ = CalibratorPhase::Idle;
        window_.cancel();
        say("cal: cancelled; %s", keptText());
        return;

    case CommandKind::Tare:
        if (!calibrated()) {
            say("tare: refused, not calibrated (a tare only moves the zero of a calibration; calibrate first)");
            return;
        }
        if (phase_ != CalibratorPhase::Idle) {
            say("tare: busy with a calibration; type \"cal cancel\" first");
            return;
        }
        phase_ = CalibratorPhase::CollectingTare;
        window_.start(now_ms);
        say("tare: collecting; keep the arm still with nothing hanging on it");
        return;

    case CommandKind::CalStatus:
        sayStatus();
        return;

    case CommandKind::None:
        return;
    }
}

static const char* phaseText(CalibratorPhase phase) {
    switch (phase) {
    case CalibratorPhase::Idle:
        return "idle";
    case CalibratorPhase::CollectingUnloaded:
        return "collecting the unloaded reading";
    case CalibratorPhase::AwaitingLoad:
        return "waiting for \"cal loaded <x>N|kg\"";
    case CalibratorPhase::CollectingLoaded:
        return "collecting the loaded reading";
    case CalibratorPhase::CollectingTare:
        return "collecting a tare";
    }
    return "?";
}

void LoadCellCalibrator::sayStatus() {
    if (!calibrated()) {
        say("cal status: not calibrated (%s); %s", storeLoadName(boot_status_), phaseText(phase_));
        return;
    }
    say("cal status: calibrated offset=%.1f counts scale=%.4f counts/N reference=%.3f N calibration_version=%lu "
        "tare_version=%lu; %s",
        active_.offset_counts, active_.counts_per_n, active_.reference_force_n,
        static_cast<unsigned long>(active_.calibration_version), static_cast<unsigned long>(active_.tare_version),
        phaseText(phase_));
}

void LoadCellCalibrator::fail(const char* what, const char* why) {
    const bool tare = phase_ == CalibratorPhase::CollectingTare;
    phase_ = CalibratorPhase::Idle;
    window_.cancel();
    say("%s FAILED: %s: %s; %s", tare ? "tare" : "cal", what, why, tare ? "previous zero kept" : keptText());
}

void LoadCellCalibrator::acceptTare(double offset_counts) {
    // Offset only; the scale, reference readings and calibration_version stay.
    // Not saved: a tare is repeated after every power-up (2026-11-09).
    CalibrationRecord tared = active_;
    tared.offset_counts = offset_counts;
    tared.tare_version = active_.tare_version + 1;
    active_ = tared;
    phase_ = CalibratorPhase::Idle;
    say("tare OK: offset=%.1f counts calibration_version=%lu tare_version=%lu (not saved: tare again after a reboot)",
        active_.offset_counts, static_cast<unsigned long>(active_.calibration_version),
        static_cast<unsigned long>(active_.tare_version));
}

bool LoadCellCalibrator::save(const CalibrationRecord& record) {
    uint8_t bytes[kCalibrationRecordBytes];
    encodeCalibration(record, bytes);
    if (!store_.write(bytes, sizeof bytes)) {
        return false;
    }
    uint8_t back[kCalibrationRecordBytes] = {};
    const StoreRead read = store_.read(back, sizeof back);
    return read.present && read.length == sizeof bytes && memcmp(back, bytes, sizeof bytes) == 0;
}

void LoadCellCalibrator::acceptCalibration(double loaded_counts) {
    const ScaleResult scale = scaleFromPoints(unloaded_counts_, loaded_counts, loaded_command_.reference_force_n,
                                              config_.min_separation_counts);
    if (scale.error != CalibrationError::None) {
        fail("calibration rejected", calibrationErrorName(scale.error));
        return;
    }

    CalibrationRecord candidate; // kept apart from active_ until saved
    candidate.valid = true;
    candidate.offset_counts = unloaded_counts_;
    candidate.counts_per_n = scale.counts_per_n;
    candidate.reference_force_n = loaded_command_.reference_force_n;
    candidate.reference_mass_kg = loaded_command_.reference_mass_kg;
    candidate.gravity_m_s2 = isnan(loaded_command_.reference_mass_kg) ? NAN : kStandardGravityMs2;
    candidate.unloaded_counts = unloaded_counts_;
    candidate.loaded_counts = loaded_counts;
    candidate.calibration_version = active_.calibration_version + 1;
    candidate.tare_version = active_.tare_version + 1;

    if (!save(candidate)) {
        // Storage may now hold a damaged or half-written record. Put the
        // active one back so a reboot finds what is in use now.
        const bool restored = !calibrated() || save(active_);
        fail("could not save", restored ? "storage write failed" : "storage write failed, and so did restoring the "
                                                                   "previous record (it is lost at the next reboot)");
        return;
    }
    active_ = candidate; // the one swap: offset, scale and versions together
    phase_ = CalibratorPhase::Idle;
    say("cal OK: offset=%.1f counts scale=%.4f counts/N reference=%.3f N calibration_version=%lu "
        "tare_version=%lu (saved)",
        active_.offset_counts, active_.counts_per_n, active_.reference_force_n,
        static_cast<unsigned long>(active_.calibration_version), static_cast<unsigned long>(active_.tare_version));
}

void LoadCellCalibrator::collectionFinished(const WindowResult& result) {
    if (result.status == WindowStatus::Failed) {
        fail("reading not stable", windowFailureName(result.failure));
        return;
    }
    switch (phase_) {
    case CalibratorPhase::CollectingUnloaded:
        unloaded_counts_ = result.mean_counts;
        phase_ = CalibratorPhase::AwaitingLoad;
        say("cal: unloaded reading %.1f counts (spread %ld, %lu samples); hang the reference load, then type "
            "\"cal loaded <x>N\" or \"cal loaded <x>kg\"",
            result.mean_counts, static_cast<long>(result.spread_counts), static_cast<unsigned long>(result.samples));
        return;
    case CalibratorPhase::CollectingLoaded:
        acceptCalibration(result.mean_counts);
        return;
    case CalibratorPhase::CollectingTare:
        acceptTare(result.mean_counts);
        return;
    default:
        return;
    }
}

ForceMeasurement LoadCellCalibrator::step(const LoadCellSample& sample, uint32_t now_ms) {
    if (window_.collecting()) {
        const WindowStatus status = window_.step(sample, now_ms);
        if (status == WindowStatus::Stable || status == WindowStatus::Failed) {
            collectionFinished(window_.result());
        }
    }

    ForceMeasurement f;
    f.timestamp_ms = sample.timestamp_ms;
    if (phase_ != CalibratorPhase::Idle || !calibrated()) {
        return f; // calibrating, or nothing to convert with
    }
    f.calibration_ready = true;
    f.calibration_version = active_.calibration_version;
    f.tare_version = active_.tare_version;
    if (sample.status == LoadCellStatus::Valid) {
        const ForceConversion c = forceFromRaw(active_, sample.raw);
        f.available = c.valid;
        f.force_n = c.force_n;
    }
    return f;
}
