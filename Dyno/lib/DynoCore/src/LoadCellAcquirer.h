// Turns a LoadCellProvider into a steady stream of LoadCellSamples, without
// ever waiting. Call step(now_ms) often (every few ms); it does at most one
// "ready?" check and one read, and handles timeouts, faults and recovery.
#pragma once

#include <stdint.h>

#include "LoadCellProvider.h"
#include "LoadCellSample.h"

struct LoadCellAcquirerConfig {
    uint32_t ready_deadline_ms;    // no reading this long after the last one = one timeout
    uint32_t retry_limit;          // this many timeouts in a row = Fault
    uint32_t recovery_interval_ms; // after a Fault: power-cycle, then leave the chip alone this long
};

struct LoadCellCounters {
    uint32_t timeouts = 0;       // deadlines missed (total)
    uint32_t faults_entered = 0; // times the status became Fault
    uint32_t recoveries = 0;     // times a good reading ended a Fault
    uint32_t samples = 0;        // good readings taken
};

class LoadCellAcquirer {
  public:
    LoadCellAcquirer(LoadCellProvider& provider, const LoadCellAcquirerConfig& config);

    // Returns true when sample() changed (new reading or new status).
    bool step(uint32_t now_ms);

    LoadCellSample sample() const { return sample_; }
    LoadCellCounters counters() const { return counters_; }

  private:
    LoadCellProvider& provider_;
    LoadCellAcquirerConfig config_;
    bool started_ = false;
    uint32_t attempt_start_ms_ = 0; // when we started waiting for the next reading
    uint32_t consecutive_timeouts_ = 0;
    bool recovering_ = false;
    uint32_t recovery_start_ms_ = 0; // when the chip was power-cycled
    LoadCellSample sample_;
    LoadCellCounters counters_;
};
