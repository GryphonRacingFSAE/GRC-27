#include "FakeCanBus.h"

int32_t fakeEcuRawValue(EcuSignal signal, uint32_t script_t_ms) {
    switch (signal) {
    case EcuSignal::EngineSpeed:       return static_cast<int32_t>(1000 + script_t_ms / 2); // ramp, rpm
    case EcuSignal::LambdaAverage:     return 1000;  // 1.000
    case EcuSignal::LambdaA:           return 990;   // 0.990
    case EcuSignal::LambdaB:           return 1010;  // 1.010
    case EcuSignal::FuelInjPulsewidth: return 1100;  // 11.00 ms
    case EcuSignal::FuelInjDuty:       return 450;   // 45.0 %
    case EcuSignal::KnockDetected:     return 0;
    case EcuSignal::RevLimitRpm:       return 11000; // rpm
    case EcuSignal::LambdaTarget:      return 950;   // 0.950
    case EcuSignal::BatteryVoltage:    return 1326;  // 13.26 V
    case EcuSignal::IntakeAirTemp:     return 346;   // 34.6 degC
    case EcuSignal::CoolantTemp:       return 885;   // 88.5 degC
    case EcuSignal::GearPosn:          return 1;
    case EcuSignal::Count:             break;
    }
    return 0;
}

FakeCanBus::FakeCanBus(const FakeEcuScript& script) : script_(script) {}

void FakeCanBus::setTime(uint32_t now_ms) {
    now_ms_ = now_ms;
}

bool FakeCanBus::start() {
    ++starts_;
    if (starts_to_fail_ > 0) {
        --starts_to_fail_;
        return false;
    }
    state_ = CanBusState::Running;
    // A stopped controller hears nothing: frames sent before now are gone.
    const uint32_t elapsed = now_ms_ - script_.origin_ms;
    if (script_.period_ms != 0 && static_cast<int32_t>(elapsed - next_slot_ms_) > 0) {
        next_slot_ms_ = (elapsed + script_.period_ms - 1) / script_.period_ms * script_.period_ms;
        next_msg_ = 0;
    }
    return true;
}

bool FakeCanBus::beginRecovery() {
    ++recovery_requests_;
    if (state_ != CanBusState::BusOff) {
        return false;
    }
    if (recoveries_to_refuse_ > 0) {
        --recoveries_to_refuse_;
        return false;
    }
    state_ = CanBusState::Recovering;
    return true;
}

void FakeCanBus::finishRecovery() {
    if (state_ == CanBusState::Recovering) {
        state_ = CanBusState::Stopped;
    }
}

bool FakeCanBus::inject(uint32_t id, bool extended, bool remote, uint8_t dlc, const uint8_t* data) {
    if (injected_count_ == kInjectSlots) {
        return false;
    }
    Injected& slot = injected_[(injected_head_ + injected_count_) % kInjectSlots];
    slot = Injected{};
    slot.id = id;
    slot.extended = extended;
    slot.remote = remote;
    slot.dlc = dlc;
    slot.has_data = data != nullptr;
    for (int i = 0; data != nullptr && i < 8 && i < dlc; ++i) {
        slot.data[i] = data[i];
    }
    ++injected_count_;
    return true;
}

CanReceiveResult FakeCanBus::receive(CanFrame& out, uint32_t timeout_ms) {
    ++receive_calls_;
    if (timeout_ms > longest_timeout_ms_) {
        longest_timeout_ms_ = timeout_ms;
    }
    if (state_ != CanBusState::Running) {
        ++receives_not_running_;
        return CanReceiveResult::Error;
    }
    if (receives_to_fail_ > 0) {
        --receives_to_fail_;
        return CanReceiveResult::Error;
    }
    if (injected_count_ > 0) {
        const Injected& in = injected_[injected_head_];
        for (int i = 0; i < 8; ++i) {
            payload_[i] = in.data[i];
        }
        out = CanFrame{};
        out.id = in.id;
        out.extended = in.extended;
        out.remote = in.remote;
        out.dlc = in.dlc;
        out.data = in.has_data ? payload_ : nullptr;
        out.rx_ms = now_ms_;
        injected_head_ = (injected_head_ + 1) % kInjectSlots;
        --injected_count_;
        return CanReceiveResult::Frame;
    }
    return nextScriptedFrame(out) ? CanReceiveResult::Frame : CanReceiveResult::Timeout;
}

uint8_t FakeCanBus::maskAt(uint32_t script_t_ms) const {
    for (size_t i = 0; i < script_.phase_count; ++i) {
        const FakeEcuPhase& p = script_.phases[i];
        if (script_t_ms >= p.from_ms && script_t_ms < p.to_ms) {
            return p.message_mask;
        }
    }
    return 0;
}

// Little-endian encode of every signal in message m (the decoder's inverse).
void FakeCanBus::fillPayload(EcuMessage m, uint32_t cycle_t_ms) {
    uint64_t all = 0;
    for (const EcuSignalDef& sig : kEcuSignals) {
        if (sig.message != m) {
            continue;
        }
        const uint64_t mask = (uint64_t{1} << sig.length) - 1;
        const uint64_t bits = static_cast<uint32_t>(fakeEcuRawValue(sig.signal, cycle_t_ms)) & mask;
        all |= bits << sig.start_bit;
    }
    for (int i = 0; i < 8; ++i) {
        payload_[i] = static_cast<uint8_t>(all >> (8 * i));
    }
}

bool FakeCanBus::nextScriptedFrame(CanFrame& out) {
    if (script_.period_ms == 0) {
        return false;
    }
    const uint32_t elapsed = now_ms_ - script_.origin_ms;
    // Signed difference: slots up to now are due; correct across millis() wrap.
    while (static_cast<int32_t>(elapsed - next_slot_ms_) >= 0) {
        const uint32_t cycle_t = script_.repeat_ms ? next_slot_ms_ % script_.repeat_ms : next_slot_ms_;
        const uint8_t mask = maskAt(cycle_t);
        while (next_msg_ < kEcuMessageCount) {
            const uint8_t m = next_msg_++;
            if (mask & (1u << m)) {
                fillPayload(static_cast<EcuMessage>(m), cycle_t);
                out = CanFrame{};
                out.id = kEcuMessages[m].id;
                out.dlc = 8;
                out.data = payload_;
                out.rx_ms = script_.origin_ms + next_slot_ms_;
                return true;
            }
        }
        next_msg_ = 0;
        next_slot_ms_ += script_.period_ms;
    }
    return false;
}

static const FakeEcuPhase kDemoPhases[] = {
    // 0-2 s: silent (startup, ECU not talking yet)
    {2000, 10000, 0x7F},  // normal: all 7 messages
    {10000, 15000, 0x5F}, // 0x530 (bit 5) missing -> goes stale on its own
    // 15-20 s: silent -> everything stale
    {20000, 25000, 0x7F}, // recovery
};

const FakeEcuScript& demoEcuScript() {
    static const FakeEcuScript script = {kDemoPhases, sizeof(kDemoPhases) / sizeof(kDemoPhases[0]),
                                         /*period_ms=*/20, /*origin_ms=*/0, /*repeat_ms=*/25000};
    return script;
}
