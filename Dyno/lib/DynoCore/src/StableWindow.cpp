#include "StableWindow.h"

void StableWindow::start(uint32_t now_ms) {
    start_ms_ = now_ms;
    have_last_ = false;
    head_ = 0;
    count_ = 0;
    result_ = WindowResult();
    result_.status = WindowStatus::Collecting;
}

void StableWindow::cancel() {
    count_ = 0;
    result_ = WindowResult();
}

void StableWindow::finish(WindowStatus status, WindowFailure failure, uint32_t now_ms) {
    result_.status = status;
    result_.failure = failure;
    result_.finished_ms = now_ms;
}

WindowStatus StableWindow::step(const LoadCellSample& sample, uint32_t now_ms) {
    if (result_.status != WindowStatus::Collecting) {
        return result_.status;
    }

    if (sample.status == LoadCellStatus::Fault) {
        finish(WindowStatus::Failed, WindowFailure::SensorFault, now_ms);
        return result_.status;
    }

    // A new sample: Valid, with a timestamp we haven't seen. (A sample from
    // before start() needs no check: stability is only judged after a whole
    // window has passed, by when anything older has aged out.)
    const bool is_new = sample.status == LoadCellStatus::Valid && (!have_last_ || sample.timestamp_ms != last_ts_);
    if (is_new) {
        have_last_ = true;
        last_ts_ = sample.timestamp_ms;
        if (sample.raw >= config_.saturation_counts || sample.raw <= -config_.saturation_counts) {
            finish(WindowStatus::Failed, WindowFailure::Saturated, now_ms);
            return result_.status;
        }
        if (count_ == kMaxSamples) { // full: forget the oldest
            head_ = (head_ + 1) % kMaxSamples;
            --count_;
        }
        const size_t slot = (head_ + count_) % kMaxSamples;
        raw_[slot] = sample.raw;
        ts_[slot] = sample.timestamp_ms;
        ++count_;
    }

    // Drop samples older than the window (age >= window_ms).
    while (count_ > 0 && now_ms - ts_[head_] >= config_.window_ms) {
        head_ = (head_ + 1) % kMaxSamples;
        --count_;
    }

    int32_t lo = 0;
    int32_t hi = 0;
    int64_t sum = 0;
    for (size_t i = 0; i < count_; ++i) {
        const int32_t v = raw_[(head_ + i) % kMaxSamples];
        if (i == 0 || v < lo) lo = v;
        if (i == 0 || v > hi) hi = v;
        sum += v;
    }
    result_.samples = static_cast<uint32_t>(count_);
    result_.spread_counts = count_ ? hi - lo : 0;

    const uint32_t elapsed = now_ms - start_ms_;
    if (elapsed >= config_.window_ms && count_ >= config_.min_samples &&
        result_.spread_counts <= config_.max_spread_counts) {
        result_.mean_counts = static_cast<double>(sum) / static_cast<double>(count_);
        finish(WindowStatus::Stable, WindowFailure::None, now_ms);
        return result_.status;
    }
    if (elapsed >= config_.timeout_ms) {
        finish(WindowStatus::Failed,
               count_ < config_.min_samples ? WindowFailure::InsufficientSamples : WindowFailure::Unstable, now_ms);
    }
    return result_.status;
}

const char* windowFailureName(WindowFailure failure) {
    switch (failure) {
    case WindowFailure::None:
        return "None";
    case WindowFailure::SensorFault:
        return "SensorFault";
    case WindowFailure::Saturated:
        return "Saturated";
    case WindowFailure::InsufficientSamples:
        return "InsufficientSamples";
    case WindowFailure::Unstable:
        return "Unstable";
    }
    return "?";
}
