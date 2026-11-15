// The stored form of an accepted calibration (#35): a fixed 76-byte record,
// little-endian, ending in a CRC-32 so a damaged or half-written record is
// recognised instead of being used.
//
//   offset  size  field
//        0     4  magic "DCAL"
//        4     2  schema id (kCalibrationSchema); a new layout gets a new id
//        6     2  record length (76)
//        8     8  offset_counts        (double)
//       16     8  counts_per_n         (double, signed)
//       24     8  reference_force_n    (double)
//       32     8  reference_mass_kg    (double, NaN if given in N)
//       40     8  gravity_m_s2         (double, NaN if given in N)
//       48     8  unloaded_counts      (double)
//       56     8  loaded_counts        (double)
//       64     4  calibration_version  (uint32)
//       68     4  tare_version         (uint32)
//       72     4  CRC-32 of bytes 0..71 (IEEE, as zlib's crc32)
//
// Only accepted records are ever written, so there is no "valid" flag: a
// record that decodes is valid; anything else is Missing, Corrupt or
// UnsupportedSchema, and the dyno then has no calibration.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CalibrationMath.h"
#include "CalibrationStore.h"

constexpr uint16_t kCalibrationSchema = 1;
constexpr size_t kCalibrationRecordBytes = 76;

enum class StoreLoad : uint8_t {
    Ok,
    Missing,           // nothing stored (first boot, or erased)
    Corrupt,           // wrong size, bad magic, bad CRC or unusable numbers
    UnsupportedSchema, // written by a different firmware's layout
};

struct DecodedCalibration {
    StoreLoad status = StoreLoad::Missing;
    CalibrationRecord record; // valid only when status is Ok
};

// record must be usable (calibrationUsable); writes kCalibrationRecordBytes.
void encodeCalibration(const CalibrationRecord& record, uint8_t out[kCalibrationRecordBytes]);

DecodedCalibration decodeCalibration(const StoreRead& read, const uint8_t* bytes);

// Reads and decodes whatever the store holds.
DecodedCalibration loadCalibration(CalibrationStore& store);

uint32_t crc32(const uint8_t* data, size_t length);

const char* storeLoadName(StoreLoad status);
