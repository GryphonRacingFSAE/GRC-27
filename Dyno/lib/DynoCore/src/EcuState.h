// The latest decoded ECU data, as shared with the rest of the dyno.
// Plain data (copied whole through a FreeRTOS mailbox), no pointers.
#pragma once

#include <stdint.h>

#include "EcuSignals.h"

struct EcuMessageStatus {
    bool received = false;   // a valid frame of this message has arrived since boot
    uint32_t last_rx_ms = 0; // millis() of the latest one (meaningless until received)
};

struct EcuState {
    // Raw DBC numbers, exactly as sent (u16 0..65535, i16 -32768..32767, bit
    // 0/1). Signals of a never-received message stay 0 and must not be used.
    int32_t raw_values[kEcuSignalCount] = {};
    EcuMessageStatus messages[kEcuMessageCount] = {};

    int32_t raw(EcuSignal s) const { return raw_values[static_cast<size_t>(s)]; }
    // Physical value: raw x scale (e.g. lambda raw 1000 -> 1.000).
    double value(EcuSignal s) const { return raw(s) * ecuSignalDef(s).scale; }

    const EcuMessageStatus& message(EcuMessage m) const { return messages[static_cast<size_t>(m)]; }

    // Received at least once and the latest one is younger than freshness_ms.
    // Take now_ms AFTER copying the state out of its mailbox: a now_ms older
    // than last_rx_ms looks like a ~49-day age and reads as stale.
    bool isFresh(EcuMessage m, uint32_t now_ms, uint32_t freshness_ms) const {
        const EcuMessageStatus& s = message(m);
        // Unsigned subtraction: the age is right even when millis() wraps.
        return s.received && (now_ms - s.last_rx_ms) < freshness_ms;
    }

    // Bit n = message n of EcuMessage (bit 0 = 0x520). Same bits as the CSV.
    uint8_t receivedMask() const {
        uint8_t mask = 0;
        for (size_t m = 0; m < kEcuMessageCount; ++m) {
            if (messages[m].received) {
                mask |= static_cast<uint8_t>(1u << m);
            }
        }
        return mask;
    }

    uint8_t freshMask(uint32_t now_ms, uint32_t freshness_ms) const {
        uint8_t mask = 0;
        for (size_t m = 0; m < kEcuMessageCount; ++m) {
            if (isFresh(static_cast<EcuMessage>(m), now_ms, freshness_ms)) {
                mask |= static_cast<uint8_t>(1u << m);
            }
        }
        return mask;
    }
};
