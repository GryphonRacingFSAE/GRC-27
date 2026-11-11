// Runs a CanBus: starts it (and retries if that fails), receives frames
// with a bounded wait, decodes them into an EcuState, and recovers from
// bus-off. Call step(now_ms) in a loop; it never waits longer than
// config.rx_timeout_ms.
#pragma once

#include <stdint.h>

#include "CanBus.h"
#include "EcuState.h"

struct CanReceiverConfig {
    uint32_t rx_timeout_ms;       // longest a single step() waits for a frame
    uint32_t restart_interval_ms; // after a failed start(), wait this long before trying again
};

enum class CanLinkStatus : uint8_t {
    Starting,    // start() not tried yet
    Running,     // receiving (says nothing about whether the ECU is talking: see freshness)
    StartFailed, // start() failed; retrying every restart_interval_ms
    Recovering,  // bus-off happened; recovery in progress
};

static constexpr uint32_t kUnknownIdSlots = 8;
static constexpr uint32_t kExtendedIdFlag = 0x80000000u; // marks a 29-bit ID in unknown_ids

struct CanStatus {
    CanLinkStatus link = CanLinkStatus::Starting;
    uint32_t frames_decoded = 0;   // accepted ECU frames
    uint32_t frames_ignored = 0;   // not ours: unknown ID, extended ID or remote request
    uint32_t frames_malformed = 0; // our ID, but wrong length / no payload
    uint32_t rx_errors = 0;        // driver receive errors
    uint32_t rx_missed = 0;        // lost because the driver's queue was full
    // The controller's own error counters (see CanBusErrors). Climbing
    // bus_errors with no frames decoded = wrong bit rate or wiring, not a
    // silent ECU.
    uint32_t bus_errors = 0;
    uint32_t rx_error_counter = 0;
    uint32_t tx_error_counter = 0;
    bool error_passive = false;    // either error counter >= 128
    uint32_t bus_off_events = 0;
    uint32_t recovery_failures = 0; // driver refused to start bus-off recovery (retried every restart interval)
    uint32_t recoveries = 0;       // bus-offs that ended with the bus running again
    uint32_t start_failures = 0;
    uint32_t message_frames[kEcuMessageCount] = {}; // accepted frames per message (rates)

    // The first distinct IDs that weren't ours (bit 31 set = extended ID).
    // If the ECU uses different IDs than the DBC, they show up here.
    uint32_t unknown_ids[kUnknownIdSlots] = {};
    uint8_t unknown_id_count = 0;
    bool unknown_ids_overflowed = false; // more distinct IDs seen than slots
};

enum class CanStepResult : uint8_t {
    Decoded, // an ECU frame changed the EcuState: publish it
    Frame,   // a frame arrived but wasn't ours (or was bad)
    Idle,    // nothing arrived: the caller may sleep a little
};

class CanReceiver {
  public:
    CanReceiver(CanBus& bus, const CanReceiverConfig& config);

    CanStepResult step(uint32_t now_ms);

    const EcuState& ecu() const { return ecu_; }
    const CanStatus& status() const { return status_; }

  private:
    CanStepResult tryStart(uint32_t now_ms, bool immediately);
    CanStepResult receiveOne();
    void noteUnknownId(const CanFrame& frame);
    void requestRecovery(uint32_t now_ms);
    void refreshErrors();

    CanBus& bus_;
    CanReceiverConfig config_;
    bool start_attempted_ = false;
    uint32_t last_start_attempt_ms_ = 0;
    // A bus-off happened and the bus hasn't run again since. Not the same as
    // link == Recovering: a failed restart after recovery moves the link to
    // StartFailed, but the eventual successful start still counts as a recovery.
    bool awaiting_restart_after_bus_off_ = false;
    uint32_t last_recovery_request_ms_ = 0;
    EcuState ecu_;
    CanStatus status_;
};
