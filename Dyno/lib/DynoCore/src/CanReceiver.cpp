#include "CanReceiver.h"

#include "EcuDecoder.h"

CanReceiver::CanReceiver(CanBus& bus, const CanReceiverConfig& config) : bus_(bus), config_(config) {}

CanStepResult CanReceiver::tryStart(uint32_t now_ms, bool immediately) {
    // Unsigned subtraction: correct even when millis() wraps around.
    if (!immediately && start_attempted_ && now_ms - last_start_attempt_ms_ < config_.restart_interval_ms) {
        return CanStepResult::Idle;
    }
    start_attempted_ = true;
    last_start_attempt_ms_ = now_ms;
    if (!bus_.start()) {
        ++status_.start_failures;
        status_.link = CanLinkStatus::StartFailed;
        return CanStepResult::Idle;
    }
    status_.link = CanLinkStatus::Running;
    if (awaiting_restart_after_bus_off_) {
        awaiting_restart_after_bus_off_ = false;
        ++status_.recoveries;
    }
    return receiveOne();
}

CanStepResult CanReceiver::receiveOne() {
    CanFrame frame;
    switch (bus_.receive(frame, config_.rx_timeout_ms)) {
    case CanReceiveResult::Frame:
        break;
    case CanReceiveResult::Timeout:
        return CanStepResult::Idle;
    case CanReceiveResult::Error:
        ++status_.rx_errors;
        return CanStepResult::Idle;
    }
    switch (decodeEcuFrame(frame, ecu_)) {
    case DecodeResult::Accepted:
        ++status_.frames_decoded;
        if (const EcuMessageDef* msg = findEcuMessage(frame.id)) { // always found once Accepted
            ++status_.message_frames[static_cast<size_t>(msg->message)];
        }
        return CanStepResult::Decoded;
    case DecodeResult::UnknownId:
    case DecodeResult::Extended:
        ++status_.frames_ignored;
        noteUnknownId(frame);
        break;
    case DecodeResult::Remote:
        ++status_.frames_ignored;
        break;
    case DecodeResult::BadLength:
    case DecodeResult::NullPayload:
        ++status_.frames_malformed;
        break;
    }
    return CanStepResult::Frame;
}

void CanReceiver::noteUnknownId(const CanFrame& frame) {
    const uint32_t key = frame.extended ? (frame.id | kExtendedIdFlag) : frame.id;
    for (uint8_t i = 0; i < status_.unknown_id_count; ++i) {
        if (status_.unknown_ids[i] == key) {
            return;
        }
    }
    if (status_.unknown_id_count < kUnknownIdSlots) {
        status_.unknown_ids[status_.unknown_id_count++] = key;
    } else {
        status_.unknown_ids_overflowed = true;
    }
}

void CanReceiver::requestRecovery(uint32_t now_ms) {
    last_recovery_request_ms_ = now_ms;
    if (!bus_.beginRecovery()) {
        ++status_.recovery_failures;
    }
}

// CAN spec: error passive once either error counter reaches 128.
static constexpr uint32_t kErrorPassiveThreshold = 128;

void CanReceiver::refreshErrors() {
    const CanBusErrors e = bus_.errors();
    status_.rx_missed = e.rx_missed;
    status_.bus_errors = e.bus_errors;
    status_.rx_error_counter = e.rx_error_counter;
    status_.tx_error_counter = e.tx_error_counter;
    status_.error_passive =
        e.rx_error_counter >= kErrorPassiveThreshold || e.tx_error_counter >= kErrorPassiveThreshold;
}

CanStepResult CanReceiver::step(uint32_t now_ms) {
    refreshErrors(); // every step, whatever the link state, so nothing looks frozen
    switch (status_.link) {
    case CanLinkStatus::Starting:
    case CanLinkStatus::StartFailed:
        return tryStart(now_ms, false);

    case CanLinkStatus::Recovering:
        switch (bus_.state()) {
        case CanBusState::Stopped: // recovery finished: the driver needs start() again
            return tryStart(now_ms, true);
        case CanBusState::BusOff: // our recovery request was refused: ask again, slowly
            if (now_ms - last_recovery_request_ms_ >= config_.restart_interval_ms) {
                requestRecovery(now_ms);
            }
            return CanStepResult::Idle;
        case CanBusState::Recovering: // still recovering: leave it alone
            return CanStepResult::Idle;
        case CanBusState::Running: // came back by itself
            status_.link = CanLinkStatus::Running;
            ++status_.recoveries;
            awaiting_restart_after_bus_off_ = false;
            return CanStepResult::Idle;
        }
        return CanStepResult::Idle;

    case CanLinkStatus::Running:
        break;
    }

    switch (bus_.state()) {
    case CanBusState::Running:
        return receiveOne();
    case CanBusState::BusOff:
        ++status_.bus_off_events;
        awaiting_restart_after_bus_off_ = true;
        status_.link = CanLinkStatus::Recovering;
        requestRecovery(now_ms);
        return CanStepResult::Idle;
    case CanBusState::Recovering:
        awaiting_restart_after_bus_off_ = true;
        status_.link = CanLinkStatus::Recovering;
        return CanStepResult::Idle;
    case CanBusState::Stopped:
        status_.link = CanLinkStatus::Starting; // stopped behind our back: start again
        return tryStart(now_ms, true);
    }
    return CanStepResult::Idle;
}
