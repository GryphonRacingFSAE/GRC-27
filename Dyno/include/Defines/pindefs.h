// ============================================================================
// Dyno pin assignments. The ONLY place pin numbers may appear.
//
// TBD: placeholder values copied from the GRC26 Dyno prototype. NOT confirmed
// for the GRC27 Dyno board. Confirm against the board schematic, then change
// the numbers and set DYNO_PINS_CONFIRMED to 1. No other code changes needed.
// ============================================================================
#pragma once

#define DYNO_PINS_CONFIRMED 0  // TBD: 1 once checked against the GRC27 schematic

static constexpr int PIN_HX711_DOUT = 16;  // TBD (GRC26 "STRAIN_AMP_DATA")
static constexpr int PIN_HX711_SCK  = 15;  // TBD (GRC26 "STRAIN_AMP_CLK")
static constexpr int PIN_CAN_TX     = 40;  // TBD (GRC26 Dyno; to the transceiver's TXD)
static constexpr int PIN_CAN_RX     = 41;  // TBD (GRC26 Dyno; from the transceiver's RXD)

// Add every new pin to this list so clashes fail the build.
constexpr bool dynoPinsAreDistinct() {
    constexpr int pins[] = {PIN_HX711_DOUT, PIN_HX711_SCK, PIN_CAN_TX, PIN_CAN_RX};
    constexpr int n = sizeof(pins) / sizeof(pins[0]);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (pins[i] == pins[j]) {
                return false;
            }
        }
    }
    return true;
}
static_assert(dynoPinsAreDistinct(), "pindefs.h: two Dyno functions share a GPIO");
