// The ESP32-S3's CAN controller ("TWAI" in Espressif's docs) behind the
// CanBus interface. Uses the legacy driver/twai.h API shipped with the
// installed framework (Arduino-ESP32 on ESP-IDF 5.5). See docs/can-decoding.md.
#pragma once

#include <CanBus.h>
#include <driver/twai.h>

class TwaiCanBus : public CanBus {
  public:
    // NORMAL: the dyno acknowledges frames (needed if the ECU is its only
    // partner on the bus). NO_ACK: self-test only (loopback test).
    TwaiCanBus(int tx_pin, int rx_pin, uint32_t bitrate_kbps, uint32_t rx_queue_len,
               twai_mode_t mode = TWAI_MODE_NORMAL);

    bool start() override;
    CanReceiveResult receive(CanFrame& out, uint32_t timeout_ms) override;
    CanBusState state() override;
    bool beginRecovery() override;
    CanBusErrors errors() override;

    // Self-test only (NO_ACK mode): send a frame that this controller also
    // receives. The dyno never transmits in normal use.
    bool transmitToSelf(uint32_t id, bool extended, bool remote, uint8_t dlc, const uint8_t* data);

    // The esp_err_t of the last failed driver call (0 = none), for diagnostics.
    int lastError() const { return last_error_; }

  private:
    int tx_pin_;
    int rx_pin_;
    uint32_t bitrate_kbps_;
    uint32_t rx_queue_len_;
    twai_mode_t mode_;
    bool installed_ = false;
    int last_error_ = 0;
    twai_message_t msg_ = {}; // CanFrame::data points in here until the next receive()
};
