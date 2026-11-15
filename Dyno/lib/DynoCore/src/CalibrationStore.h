// Where the accepted calibration is kept between power cycles (#35). On the
// ESP32 this is Preferences/NVS (a small key-value store in flash, see
// lib/DynoHardware/PreferencesCalibrationStore); tests use
// FakeCalibrationStore. The store only moves bytes: the record format and
// its checks live in CalibrationRecordCodec.
#pragma once

#include <stddef.h>
#include <stdint.h>

struct StoreRead {
    bool present = false; // something is stored under our key
    size_t length = 0;    // its full length; bytes are copied only if it fits
};

class CalibrationStore {
  public:
    virtual ~CalibrationStore() = default;

    // Copies the stored bytes into buf when they fit in `capacity`.
    virtual StoreRead read(uint8_t* buf, size_t capacity) = 0;

    // Replaces the stored bytes. True only if all of them were written. On
    // false the previous contents may or may not still be there; the caller
    // never assumes either.
    virtual bool write(const uint8_t* data, size_t length) = 0;
};
