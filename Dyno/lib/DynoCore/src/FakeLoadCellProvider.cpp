#include "FakeLoadCellProvider.h"

FakeLoadCellProvider::FakeLoadCellProvider(const FakeLoadCellScript& script) : script_(script) {}

void FakeLoadCellProvider::setTime(uint32_t now_ms) { now_ms_ = now_ms; }

bool FakeLoadCellProvider::latestConversion(uint64_t& id, int32_t& value) const {
    // Unsigned subtraction keeps this correct across the millis() rollover.
    const uint32_t elapsed_ms = now_ms_ - script_.origin_ms;
    uint32_t local_ms = elapsed_ms;
    uint64_t cycle = 0;
    if (script_.repeat_ms != 0) {
        cycle = elapsed_ms / script_.repeat_ms;
        local_ms = elapsed_ms % script_.repeat_ms;
    }

    for (size_t i = 0; i < script_.phase_count; ++i) {
        const FakePhase& p = script_.phases[i];
        if (local_ms < p.from_ms || local_ms >= p.to_ms) {
            continue;
        }
        if (p.mode != FakeMode::Ready || script_.period_us == 0) {
            return false;
        }
        const uint64_t into_phase_us = static_cast<uint64_t>(local_ms - p.from_ms) * 1000u;
        const uint64_t k = into_phase_us / script_.period_us;
        if (k == 0) {
            return false; // first reading of the phase hasn't finished yet
        }
        const uint64_t at_us = k * script_.period_us;
        // Unique per (repeat cycle, phase, conversion) so each is readable once.
        id = (cycle << 40) ^ (static_cast<uint64_t>(i) << 32) ^ k;
        value = static_cast<int32_t>(p.count + static_cast<int64_t>(p.count_per_s) *
                                                   static_cast<int64_t>(at_us) / 1000000);
        return true;
    }
    return false;
}

bool FakeLoadCellProvider::isReady() {
    ++ready_checks_;
    uint64_t id = 0;
    int32_t value = 0;
    if (!latestConversion(id, value)) {
        return false;
    }
    return !has_read_ || id != last_read_id_;
}

int32_t FakeLoadCellProvider::readOnce() {
    ++reads_;
    uint64_t id = 0;
    int32_t value = 0;
    if (!latestConversion(id, value) || (has_read_ && id == last_read_id_)) {
        ++reads_without_ready_;
        return 0;
    }
    has_read_ = true;
    last_read_id_ = id;
    return value;
}

void FakeLoadCellProvider::powerCycle() { ++power_cycles_; }

namespace {
const FakePhase kDemoPhases[] = {
    {0, 27000, FakeMode::Ready, 0, 1000},
    {27000, 30000, FakeMode::NeverReady, 0, 0},
};
const FakeLoadCellScript kDemoScript = {kDemoPhases, 2, 12500, 0, 30000};
} // namespace

const FakeLoadCellScript& demoLoadCellScript() { return kDemoScript; }
