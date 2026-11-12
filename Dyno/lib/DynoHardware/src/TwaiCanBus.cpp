#include "TwaiCanBus.h"

#include <Arduino.h>

static twai_timing_config_t timingFor(uint32_t kbps) {
    switch (kbps) {
    case 125: {
        const twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
        return t;
    }
    case 250: {
        const twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
        return t;
    }
    case 1000: {
        const twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
        return t;
    }
    default: { // 500 (DynoConfig.h static_asserts the allowed values)
        const twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
        return t;
    }
    }
}

TwaiCanBus::TwaiCanBus(int tx_pin, int rx_pin, uint32_t bitrate_kbps, uint32_t rx_queue_len, twai_mode_t mode)
    : tx_pin_(tx_pin), rx_pin_(rx_pin), bitrate_kbps_(bitrate_kbps), rx_queue_len_(rx_queue_len), mode_(mode) {}

bool TwaiCanBus::start() {
    if (!installed_) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(tx_pin_),
                                                              static_cast<gpio_num_t>(rx_pin_), mode_);
        g.rx_queue_len = rx_queue_len_;
        // We never transmit in normal use; the self-test needs a small queue.
        g.tx_queue_len = (mode_ == TWAI_MODE_NO_ACK) ? 5 : 0;
        const twai_timing_config_t t = timingFor(bitrate_kbps_);
        // Accept everything; EcuDecoder filters in software (and the
        // unknown-ID list then shows what else is on the bus).
        const twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        const esp_err_t err = twai_driver_install(&g, &t, &f);
        if (err != ESP_OK) {
            last_error_ = err;
            return false;
        }
        installed_ = true;
    }
    if (state() == CanBusState::Running) {
        return true; // already started: twai_start() would refuse
    }
    const esp_err_t err = twai_start();
    if (err != ESP_OK) {
        last_error_ = err;
        return false;
    }
    return true;
}

CanReceiveResult TwaiCanBus::receive(CanFrame& out, uint32_t timeout_ms) {
    const esp_err_t err = twai_receive(&msg_, pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_ERR_TIMEOUT) {
        return CanReceiveResult::Timeout;
    }
    if (err != ESP_OK) {
        last_error_ = err;
        return CanReceiveResult::Error;
    }
    out = CanFrame{};
    out.id = msg_.identifier;
    out.extended = msg_.extd;
    out.remote = msg_.rtr;
    // data_length_code can be 9..15 for non-compliant frames (dlc_non_comp);
    // only 8 bytes are ever stored, and the decoder rejects anything but 8.
    out.dlc = msg_.data_length_code;
    out.data = msg_.data;
    out.rx_ms = millis();
    return CanReceiveResult::Frame;
}

CanBusState TwaiCanBus::state() {
    if (!installed_) {
        return CanBusState::Stopped;
    }
    twai_status_info_t info;
    if (twai_get_status_info(&info) != ESP_OK) {
        return CanBusState::Stopped;
    }
    switch (info.state) {
    case TWAI_STATE_RUNNING:
        return CanBusState::Running;
    case TWAI_STATE_BUS_OFF:
        return CanBusState::BusOff;
    case TWAI_STATE_RECOVERING:
        return CanBusState::Recovering;
    case TWAI_STATE_STOPPED:
    default:
        return CanBusState::Stopped;
    }
}

bool TwaiCanBus::beginRecovery() {
    const esp_err_t err = twai_initiate_recovery();
    if (err != ESP_OK) {
        last_error_ = err;
        return false;
    }
    return true;
}

CanBusErrors TwaiCanBus::errors() {
    CanBusErrors e;
    twai_status_info_t info;
    if (!installed_ || twai_get_status_info(&info) != ESP_OK) {
        return e;
    }
    e.rx_missed = info.rx_missed_count + info.rx_overrun_count;
    e.bus_errors = info.bus_error_count;
    e.rx_error_counter = info.rx_error_counter;
    e.tx_error_counter = info.tx_error_counter;
    return e;
}

bool TwaiCanBus::transmitToSelf(uint32_t id, bool extended, bool remote, uint8_t dlc, const uint8_t* data) {
    twai_message_t m = {};
    m.identifier = id;
    m.extd = extended;
    m.rtr = remote;
    m.self = 1; // self-reception request: our own controller receives it
    m.data_length_code = dlc;
    for (int i = 0; data != nullptr && i < dlc && i < 8; ++i) {
        m.data[i] = data[i];
    }
    const esp_err_t err = twai_transmit(&m, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        last_error_ = err;
        return false;
    }
    return true;
}
