// CalibrationStore on the ESP32's NVS ("non-volatile storage": a small
// key-value store in a flash partition that survives power-off), through
// the Arduino Preferences library. One blob under one key; the record format
// and its checks are CalibrationRecordCodec's job, not this file's.
#pragma once

#include <CalibrationStore.h>

class PreferencesCalibrationStore : public CalibrationStore {
  public:
    // namespace_name: at most 15 characters (an NVS limit). The fake-inputs
    // build uses a different one so it never touches a real calibration.
    explicit PreferencesCalibrationStore(const char* namespace_name) : namespace_(namespace_name) {}

    StoreRead read(uint8_t* buf, size_t capacity) override;
    bool write(const uint8_t* data, size_t length) override;

  private:
    const char* namespace_;
};
