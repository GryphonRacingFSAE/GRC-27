// A scripted, time-driven stand-in for the ECU + CAN controller, used by host
// tests and by DYNO_FAKE_INPUTS firmware builds. Call setTime(now_ms) before
// each step. It never really waits: receive() returns Timeout at once when
// nothing is due (the caller sleeps instead).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CanBus.h"
#include "EcuSignals.h"

// One stretch of script time [from_ms, to_ms) and which ECU messages the
// fake sends in it (bit n = EcuMessage n, same as the CSV masks). Times are
// relative to the script's origin; anything not covered is silence.
struct FakeEcuPhase {
    uint32_t from_ms;
    uint32_t to_ms;
    uint8_t message_mask;
};

struct FakeEcuScript {
    const FakeEcuPhase* phases;
    size_t phase_count;
    uint32_t period_ms; // every message is sent once per period, at multiples of period_ms
    uint32_t origin_ms; // script time 0 == this absolute time
    uint32_t repeat_ms; // 0 = play once; otherwise loop every repeat_ms (a multiple of period_ms)
};

// The values the fake ECU sends at script time t (ms within the cycle):
// RPM ramps 1000 + t/2; everything else is a fixed, plausible raw value.
int32_t fakeEcuRawValue(EcuSignal signal, uint32_t script_t_ms);

class FakeCanBus : public CanBus {
  public:
    explicit FakeCanBus(const FakeEcuScript& script);

    void setTime(uint32_t now_ms);

    bool start() override;
    CanReceiveResult receive(CanFrame& out, uint32_t timeout_ms) override;
    CanBusState state() override { return state_; }
    bool beginRecovery() override;
    CanBusErrors errors() override { return errors_; }

    // ---- Fault injection (tests) ----
    void failNextStarts(uint32_t n) { starts_to_fail_ = n; }
    void failNextReceives(uint32_t n) { receives_to_fail_ = n; }
    void goBusOff() { state_ = CanBusState::BusOff; }
    void refuseNextRecoveries(uint32_t n) { recoveries_to_refuse_ = n; } // beginRecovery() fails, stays BusOff
    void finishRecovery(); // Recovering -> Stopped, like the real driver
    void setErrors(const CanBusErrors& e) { errors_ = e; } // what errors() reports from now on
    // Deliver this frame (copied, data included) before any scripted ones.
    // dlc may be anything; up to 8 bytes of `data` are copied (nullptr = no payload).
    bool inject(uint32_t id, bool extended, bool remote, uint8_t dlc, const uint8_t* data);

    // ---- What the code under test did ----
    uint32_t starts() const { return starts_; }
    uint32_t recoveryRequests() const { return recovery_requests_; }
    uint32_t receiveCalls() const { return receive_calls_; }
    uint32_t receivesWhileNotRunning() const { return receives_not_running_; }
    uint32_t longestTimeoutMs() const { return longest_timeout_ms_; }

  private:
    // Scripted frames are on a grid: slot k is at script time k*period, and
    // each slot sends the phase's messages in EcuMessage order.
    bool nextScriptedFrame(CanFrame& out);
    uint8_t maskAt(uint32_t script_t_ms) const;
    void fillPayload(EcuMessage m, uint32_t cycle_t_ms);

    static constexpr size_t kInjectSlots = 16;
    struct Injected {
        uint32_t id;
        bool extended, remote, has_data;
        uint8_t dlc;
        uint8_t data[8];
    };

    FakeEcuScript script_;
    uint32_t now_ms_ = 0;
    CanBusState state_ = CanBusState::Stopped;
    uint32_t next_slot_ms_ = 0; // script time of the next grid slot not fully delivered
    uint8_t next_msg_ = 0;   // next message index within that slot
    uint8_t payload_[8] = {};
    Injected injected_[kInjectSlots] = {};
    size_t injected_head_ = 0, injected_count_ = 0;

    uint32_t starts_to_fail_ = 0;
    uint32_t receives_to_fail_ = 0;
    uint32_t recoveries_to_refuse_ = 0;
    CanBusErrors errors_;
    uint32_t starts_ = 0;
    uint32_t recovery_requests_ = 0;
    uint32_t receive_calls_ = 0;
    uint32_t receives_not_running_ = 0;
    uint32_t longest_timeout_ms_ = 0;
};

// The DYNO_FAKE_INPUTS / #38 demo: all 7 messages at 50 Hz, in a 25 s cycle
// from boot: 0-2 s silent (startup), 2-10 s all, 10-15 s all but 0x530
// (missing frames -> 0x530 stale), 15-20 s silent (all stale), 20-25 s all
// (recovery). Then it repeats, starting silent again.
const FakeEcuScript& demoEcuScript();
