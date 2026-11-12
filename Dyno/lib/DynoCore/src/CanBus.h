// The only things CanReceiver needs from a CAN controller. The ESP32's TWAI
// driver (lib/DynoHardware/TwaiCanBus) and FakeCanBus implement it.
#pragma once

#include <stdint.h>

#include "CanFrame.h"

enum class CanBusState : uint8_t {
    Stopped,    // not started, or finished recovering (needs start() again)
    Running,    // receiving
    BusOff,     // too many errors: the controller left the bus
    Recovering, // beginRecovery() is in progress
};

enum class CanReceiveResult : uint8_t {
    Frame,   // `out` holds a frame
    Timeout, // nothing arrived within the timeout (normal when the ECU is off)
    Error,   // the driver refused (e.g. not running)
};

// The controller's own error bookkeeping. Totals are as the driver reports
// them; the two error counters follow the CAN spec (0..255, error passive at
// 128, bus-off above 255 on the transmit side).
struct CanBusErrors {
    uint32_t rx_missed = 0;        // frames lost because the receive queue was full
    uint32_t bus_errors = 0;       // bit/stuff/CRC/form/ACK errors seen on the bus
    uint32_t rx_error_counter = 0; // "REC": climbs with receive errors, falls with good frames
    uint32_t tx_error_counter = 0; // "TEC": climbs with transmit errors (ACKs/error flags we send)
};

class CanBus {
  public:
    virtual ~CanBus() = default;

    // Set up (first call only) and start the controller. false = failed.
    virtual bool start() = 0;

    // Wait at most timeout_ms for one frame. On Frame, out.data points into a
    // buffer owned by the bus that stays valid until the next receive().
    virtual CanReceiveResult receive(CanFrame& out, uint32_t timeout_ms) = 0;

    virtual CanBusState state() = 0;

    // Start bus-off recovery (only valid in BusOff). false = refused.
    virtual bool beginRecovery() = 0;

    // Error counters (all zero before the driver is set up).
    virtual CanBusErrors errors() = 0;
};
