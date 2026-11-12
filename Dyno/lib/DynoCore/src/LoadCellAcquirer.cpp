#include "LoadCellAcquirer.h"

LoadCellAcquirer::LoadCellAcquirer(LoadCellProvider& provider, const LoadCellAcquirerConfig& config)
    : provider_(provider), config_(config) {}

bool LoadCellAcquirer::step(uint32_t now_ms) {
    if (!started_) {
        started_ = true;
        attempt_start_ms_ = now_ms;
    }

    if (recovering_) {
        if (now_ms - recovery_start_ms_ < config_.recovery_interval_ms) {
            return false; // let the chip restart in peace
        }
        recovering_ = false;
        attempt_start_ms_ = now_ms; // a fresh attempt starts when recovery ends
    }

    if (!provider_.isReady()) {
        // Unsigned subtraction: correct even when millis() wraps around.
        if (now_ms - attempt_start_ms_ < config_.ready_deadline_ms) {
            return false;
        }
        ++counters_.timeouts;
        ++consecutive_timeouts_;
        attempt_start_ms_ = now_ms; // the next attempt starts right away
        if (consecutive_timeouts_ < config_.retry_limit) {
            return false;
        }
        consecutive_timeouts_ = 0;
        provider_.powerCycle();
        recovering_ = true;
        recovery_start_ms_ = now_ms;
        if (sample_.status == LoadCellStatus::Fault) {
            return false;
        }
        sample_.status = LoadCellStatus::Fault; // raw/timestamp_ms keep the last good reading
        ++counters_.faults_entered;
        return true;
    }

    if (sample_.status == LoadCellStatus::Fault) {
        ++counters_.recoveries;
    }
    sample_.raw = provider_.readOnce();
    sample_.timestamp_ms = now_ms;
    sample_.status = LoadCellStatus::Valid;
    ++counters_.samples;
    consecutive_timeouts_ = 0;
    attempt_start_ms_ = now_ms;
    return true;
}
