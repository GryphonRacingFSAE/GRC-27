// Calibrated load-cell force, as the torque/power calculation (#37) takes it.
// #35 (calibration) produces it from a LoadCellSample; until then nothing in
// the firmware does, so force is always "unavailable".
// Plain data (safe to copy through a FreeRTOS mailbox), no pointers.
#pragma once

#include <math.h>
#include <stdint.h>

struct ForceMeasurement {
    // false = no usable force: no sample yet, load-cell fault, or no accepted
    // calibration. force_n must not be used.
    bool available = false;
    double force_n = NAN;      // newtons, signed (sign comes from the calibration)
    uint32_t timestamp_ms = 0; // millis() of the raw sample this force came from

    // The calibration state this force was computed under (#35/#36).
    // calibration_ready is false while a calibration or tare is collecting;
    // the force is then unusable and its versions mean nothing.
    bool calibration_ready = false;
    // 32-bit counters; they wrap after ~4 billion changes. A value is only
    // meaningful together with calibration_ready, never on its own.
    uint32_t calibration_version = 0;
    uint32_t tare_version = 0;
};
