// In-memory CalibrationStore for host tests (#35), with knobs for the ways
// flash storage goes wrong. `bytes` is public so a test can damage a record.
#pragma once

#include <string.h>

#include <vector>

#include "CalibrationStore.h"

class FakeCalibrationStore : public CalibrationStore {
  public:
    std::vector<uint8_t> bytes;
    bool present = false;
    bool fail_writes = false;    // write() returns false and changes nothing
    bool garble_next_write = false; // the next write() claims success but stores damaged bytes
    uint32_t writes = 0;         // write() calls

    StoreRead read(uint8_t* buf, size_t capacity) override {
        StoreRead r;
        r.present = present;
        r.length = present ? bytes.size() : 0;
        if (present && bytes.size() <= capacity && !bytes.empty()) {
            memcpy(buf, bytes.data(), bytes.size());
        }
        return r;
    }

    bool write(const uint8_t* data, size_t length) override {
        ++writes;
        if (fail_writes) {
            return false;
        }
        bytes.assign(data, data + length);
        present = true;
        if (garble_next_write && !bytes.empty()) {
            bytes[bytes.size() / 2] ^= 0x5A;
            garble_next_write = false;
        }
        return true;
    }
};
