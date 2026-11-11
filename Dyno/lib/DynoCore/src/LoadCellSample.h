// The latest load-cell reading, as shared with the rest of the dyno.
#pragma once

#include <stdint.h>

enum class LoadCellStatus : uint8_t {
    NoSampleYet, // nothing has ever been read since boot
    Valid,       // raw/timestamp_ms are the latest reading
    Fault,       // sensor stopped answering; raw/timestamp_ms are the LAST GOOD
                 // reading (or 0/0 if there never was one) and must not be used
};

struct LoadCellSample {
    int32_t raw = 0;           // HX711 counts, exactly as the chip reported them
    uint32_t timestamp_ms = 0; // millis() when it was read; a change means "new sample"
    LoadCellStatus status = LoadCellStatus::NoSampleYet;
};
