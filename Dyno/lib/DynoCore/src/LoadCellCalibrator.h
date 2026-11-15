// The calibration and tare procedure (#35). Runs inside the load-cell task,
// the HX711's only owner: it is fed every sample the acquirer produces, turns
// it into force with the active calibration, and handles operator commands.
// Because the active record is only ever replaced here, between two samples,
// nobody can see a new offset with an old scale.
#pragma once

#include <stdint.h>

#include "CalibrationMath.h"
#include "CalibrationRecordCodec.h"
#include "CalibrationStore.h"
#include "CommandParser.h"
#include "ForceMeasurement.h"
#include "LoadCellSample.h"
#include "StableWindow.h"

struct CalibratorConfig {
    StableWindowConfig window;
    double min_separation_counts; // loaded - unloaded must differ by at least this
};

// Where operator feedback goes. The firmware's version passes each line to
// printDiagnostic(), which adds the "# ".
class CalibrationReplies {
  public:
    virtual ~CalibrationReplies() = default;
    virtual void reply(const char* text) = 0;
};

// Where the procedure is. Every phase but Idle counts as "calibrating":
// force is unavailable and calibration_ready is 0 until it ends.
enum class CalibratorPhase : uint8_t {
    Idle,
    CollectingUnloaded, // after "cal unloaded"
    AwaitingLoad,       // unloaded reading taken; waiting for "cal loaded <x>"
    CollectingLoaded,   // after "cal loaded <x>"
    CollectingTare,     // after "tare"
};

class LoadCellCalibrator {
  public:
    LoadCellCalibrator(CalibrationStore& store, CalibrationReplies& replies, const CalibratorConfig& config);

    // Load the stored calibration (once, at boot) and report what was found.
    void begin();

    // An operator command (from the serial parser, or #36's tare button).
    void command(const LoadCellCommand& command, uint32_t now_ms);

    // Feed the latest sample (call every loop pass, new sample or not).
    // Returns the force to publish.
    ForceMeasurement step(const LoadCellSample& sample, uint32_t now_ms);

    StoreLoad bootStatus() const { return boot_status_; }
    bool calibrated() const { return calibrationUsable(active_); }
    CalibrationRecord active() const { return active_; }
    CalibratorPhase phase() const { return phase_; }

  private:
    void say(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void fail(const char* what, const char* why);
    const char* keptText() const;
    void collectionFinished(const WindowResult& result);
    void acceptCalibration(double loaded_counts);
    void acceptTare(double offset_counts);
    void sayStatus();
    bool save(const CalibrationRecord& record);

    CalibrationStore& store_;
    CalibrationReplies& replies_;
    CalibratorConfig config_;
    StableWindow window_;
    CalibrationRecord active_;
    StoreLoad boot_status_ = StoreLoad::Missing;
    CalibratorPhase phase_ = CalibratorPhase::Idle;
    double unloaded_counts_ = 0.0;      // candidate, from CollectingUnloaded
    LoadCellCommand loaded_command_;    // the reference given with "cal loaded"
};
