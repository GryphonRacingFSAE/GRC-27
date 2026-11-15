// Waits for the load-cell reading to settle, then gives its average (#35
// step 4). Used for the unloaded and loaded calibration readings and for
// tare. It never reads the HX711 itself: the load-cell task feeds it the
// samples LoadCellAcquirer already produced, so there is still one HX711
// owner and nothing ever blocks. See docs/calibration.md ("Stability rule").
//
// The rule: look at the samples from the last `window_ms`. Once collection
// has run for a whole window, the reading is STABLE when that window holds
// at least `min_samples` samples and max - min <= `max_spread_counts`; the
// result is their mean. It slides, so the wobble while a weight is being
// hung on just ages out. It FAILS at once on a sensor fault or a saturated
// (maxed-out) sample, and after `timeout_ms` if it never settled.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "LoadCellSample.h"

struct StableWindowConfig {
    uint32_t window_ms;          // how much recent data must agree
    uint32_t min_samples;        // at least this many samples in the window
    int32_t max_spread_counts;   // max - min allowed within the window
    uint32_t timeout_ms;         // give up this long after start()
    int32_t saturation_counts;   // |raw| >= this = maxed out / junk (bridge unplugged)
};

enum class WindowStatus : uint8_t { Idle, Collecting, Stable, Failed };

enum class WindowFailure : uint8_t {
    None,
    SensorFault,         // the load cell stopped answering (LoadCellStatus::Fault)
    Saturated,           // a reading at or beyond the saturation limit
    InsufficientSamples, // timed out with fewer than min_samples in the window
    Unstable,            // timed out with enough samples but never within max spread
};

struct WindowResult {
    WindowStatus status = WindowStatus::Idle;
    WindowFailure failure = WindowFailure::None;
    double mean_counts = 0.0;   // Stable only: mean of the window
    int32_t spread_counts = 0;  // max - min of the last window looked at
    uint32_t samples = 0;       // samples in that window
    uint32_t finished_ms = 0;   // when it became Stable or Failed
};

class StableWindow {
  public:
    // Enough for 1 s at the HX711's fastest rate (80 Hz) with room to spare.
    // At a higher rate the oldest samples drop out and the window is shorter.
    static constexpr size_t kMaxSamples = 128;

    explicit StableWindow(const StableWindowConfig& config) : config_(config) {}

    // Begin collecting. Only samples taken at or after now_ms count.
    void start(uint32_t now_ms);
    // Stop and forget (back to Idle).
    void cancel();

    // Feed the latest sample (the same one again is fine: it counts once).
    // Returns the status after this step. Stable/Failed stay latched until
    // start() or cancel().
    WindowStatus step(const LoadCellSample& sample, uint32_t now_ms);

    WindowResult result() const { return result_; }
    bool collecting() const { return result_.status == WindowStatus::Collecting; }

  private:
    void finish(WindowStatus status, WindowFailure failure, uint32_t now_ms);

    StableWindowConfig config_;
    uint32_t start_ms_ = 0;
    bool have_last_ = false;
    uint32_t last_ts_ = 0;
    int32_t raw_[kMaxSamples] = {};
    uint32_t ts_[kMaxSamples] = {};
    size_t head_ = 0;  // index of the oldest stored sample
    size_t count_ = 0; // samples stored
    WindowResult result_;
};

const char* windowFailureName(WindowFailure failure);
