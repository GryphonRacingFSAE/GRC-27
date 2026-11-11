// Turns one CAN frame into ECU signal values, using the table in EcuSignals.h.
// Pure: no hardware, no clock, no RTOS.
#pragma once

#include "CanFrame.h"
#include "EcuState.h"

enum class DecodeResult : uint8_t {
    Accepted,    // one of our messages: its signals, received flag and last_rx_ms updated
    UnknownId,   // not one of our messages (normal: other devices share the bus)
    Extended,    // 29-bit ID: the ECU only sends standard frames
    Remote,      // remote request: carries no data
    BadLength,   // not exactly 8 data bytes
    NullPayload, // no data pointer
};

// On anything but Accepted, `state` is left completely untouched.
DecodeResult decodeEcuFrame(const CanFrame& frame, EcuState& state);
