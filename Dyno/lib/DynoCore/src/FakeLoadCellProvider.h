// A scripted, time-driven stand-in for the HX711, used by host tests and by
// DYNO_FAKE_INPUTS firmware builds. Call setTime(now_ms) before each step.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "LoadCellProvider.h"

enum class FakeMode : uint8_t {
    Ready,      // a new reading finishes every period
    NeverReady, // like an unplugged sensor: never has a reading
};

// One stretch of script time [from_ms, to_ms). Times are relative to the
// script's origin. In a Ready phase, readings finish at from_ms + k*period
// (k >= 1) and are worth `count + count_per_s * (seconds since from_ms)`.
struct FakePhase {
    uint32_t from_ms;
    uint32_t to_ms;
    FakeMode mode;
    int32_t count;
    int32_t count_per_s;
};

struct FakeLoadCellScript {
    const FakePhase* phases; // anything not covered by a phase is NeverReady
    size_t phase_count;
    uint32_t period_us;      // 100000 for 10 Hz, 12500 for 80 Hz
    uint32_t origin_ms;      // script time 0 == this absolute time
    uint32_t repeat_ms;      // 0 = play once; otherwise loop every repeat_ms
};

class FakeLoadCellProvider : public LoadCellProvider {
  public:
    explicit FakeLoadCellProvider(const FakeLoadCellScript& script);

    void setTime(uint32_t now_ms);

    bool isReady() override;
    int32_t readOnce() override;
    void powerCycle() override;

    // What the code under test did to the "sensor".
    uint32_t readyChecks() const { return ready_checks_; }
    uint32_t reads() const { return reads_; }
    uint32_t readsWithoutReady() const { return reads_without_ready_; }
    uint32_t powerCycles() const { return power_cycles_; }

  private:
    // The newest finished reading at the current time, if any.
    bool latestConversion(uint64_t& id, int32_t& value) const;

    FakeLoadCellScript script_;
    uint32_t now_ms_ = 0;
    bool has_read_ = false;
    uint64_t last_read_id_ = 0;
    uint32_t ready_checks_ = 0;
    uint32_t reads_ = 0;
    uint32_t reads_without_ready_ = 0;
    uint32_t power_cycles_ = 0;
};

// The DYNO_FAKE_INPUTS script: an 80 Hz ramp of 1000 counts/s starting at 0,
// "unplugged" from 27 s to 30 s, repeating every 30 s from boot. (Every ~49.7
// days, when millis() wraps, one cycle is cut short; harmless for a demo.)
const FakeLoadCellScript& demoLoadCellScript();
