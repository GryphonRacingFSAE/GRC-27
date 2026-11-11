// The only three things the acquirer needs from a load-cell chip. The real
// HX711 (src/Hardware/Hx711Provider) and FakeLoadCellProvider implement it.
#pragma once

#include <stdint.h>

class LoadCellProvider {
  public:
    virtual ~LoadCellProvider() = default;

    // Does the chip have a finished reading waiting? Must return immediately.
    virtual bool isReady() = 0;

    // Fetch the waiting reading. Only ever called right after isReady() == true.
    virtual int32_t readOnce() = 0;

    // Turn the chip off and on again (used after it stops answering).
    virtual void powerCycle() = 0;
};
