#include "CalibrationRecordCodec.h"

#include <string.h>

namespace {

constexpr size_t kCrcOffset = kCalibrationRecordBytes - 4;

void putU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}

void putU32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

void putF64(uint8_t* p, double d) {
    uint64_t v;
    memcpy(&v, &d, sizeof v);
    for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t getU32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}

double getF64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    double d;
    memcpy(&d, &v, sizeof d);
    return d;
}

const uint8_t kMagic[4] = {'D', 'C', 'A', 'L'};

} // namespace

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void encodeCalibration(const CalibrationRecord& r, uint8_t out[kCalibrationRecordBytes]) {
    memcpy(out, kMagic, 4);
    putU16(out + 4, kCalibrationSchema);
    putU16(out + 6, static_cast<uint16_t>(kCalibrationRecordBytes));
    putF64(out + 8, r.offset_counts);
    putF64(out + 16, r.counts_per_n);
    putF64(out + 24, r.reference_force_n);
    putF64(out + 32, r.reference_mass_kg);
    putF64(out + 40, r.gravity_m_s2);
    putF64(out + 48, r.unloaded_counts);
    putF64(out + 56, r.loaded_counts);
    putU32(out + 64, r.calibration_version);
    putU32(out + 68, r.tare_version);
    putU32(out + kCrcOffset, crc32(out, kCrcOffset));
}

DecodedCalibration decodeCalibration(const StoreRead& read, const uint8_t* b) {
    DecodedCalibration d;
    if (!read.present) {
        d.status = StoreLoad::Missing;
        return d;
    }
    d.status = StoreLoad::Corrupt;
    // The header is checked first so a record from another layout is
    // reported as such, whatever its length.
    if (read.length < 8 || read.length > kCalibrationRecordBytes || memcmp(b, kMagic, 4) != 0) {
        return d;
    }
    if (getU16(b + 4) != kCalibrationSchema) {
        d.status = StoreLoad::UnsupportedSchema;
        return d;
    }
    if (read.length != kCalibrationRecordBytes || getU16(b + 6) != kCalibrationRecordBytes ||
        getU32(b + kCrcOffset) != crc32(b, kCrcOffset)) {
        return d;
    }
    CalibrationRecord r;
    r.valid = true;
    r.offset_counts = getF64(b + 8);
    r.counts_per_n = getF64(b + 16);
    r.reference_force_n = getF64(b + 24);
    r.reference_mass_kg = getF64(b + 32);
    r.gravity_m_s2 = getF64(b + 40);
    r.unloaded_counts = getF64(b + 48);
    r.loaded_counts = getF64(b + 56);
    r.calibration_version = getU32(b + 64);
    r.tare_version = getU32(b + 68);
    if (!calibrationUsable(r)) {
        return d; // CRC fine but the numbers can't be used: never trust it
    }
    d.status = StoreLoad::Ok;
    d.record = r;
    return d;
}

DecodedCalibration loadCalibration(CalibrationStore& store) {
    uint8_t buf[kCalibrationRecordBytes] = {};
    const StoreRead read = store.read(buf, sizeof buf);
    return decodeCalibration(read, buf);
}

const char* storeLoadName(StoreLoad status) {
    switch (status) {
    case StoreLoad::Ok:
        return "Ok";
    case StoreLoad::Missing:
        return "Missing";
    case StoreLoad::Corrupt:
        return "Corrupt";
    case StoreLoad::UnsupportedSchema:
        return "UnsupportedSchema";
    }
    return "?";
}
