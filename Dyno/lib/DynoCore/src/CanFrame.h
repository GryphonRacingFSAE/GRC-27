// One received CAN frame, independent of the ESP32 driver's own types.
// The TWAI adapter (lib/DynoHardware/TwaiCanBus) and FakeCanBus fill these in.
#pragma once

#include <stdint.h>

struct CanFrame {
    uint32_t id = 0;              // 11-bit (standard) or 29-bit (extended) identifier
    bool extended = false;        // 29-bit ID; the ECU's frames are all standard
    bool remote = false;          // remote request: has no data at all
    uint8_t dlc = 0;              // number of data bytes the frame claims to carry
    const uint8_t* data = nullptr; // the payload; NOT owned (points at the receiver's buffer)
    uint32_t rx_ms = 0;           // millis() when the frame was received
};
