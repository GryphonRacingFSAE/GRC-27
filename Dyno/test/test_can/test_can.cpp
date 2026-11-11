// ECU CAN decoding (#33), tested with no hardware. Expected values come from
// the issue's hand-worked fixtures and from GRC27.dbc, never from the code.
#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "EcuDecoder.h"

void setUp() {}
void tearDown() {}

// Build a standard, 8-byte data frame (what the ECU sends). `data` must
// outlive the frame (it points at the caller's bytes, like the real driver).
static CanFrame frame(uint32_t id, const uint8_t* data, uint32_t rx_ms = 1000) {
    CanFrame f;
    f.id = id;
    f.extended = false;
    f.remote = false;
    f.dlc = 8;
    f.data = data;
    f.rx_ms = rx_ms;
    return f;
}

// ---------------------------------------------------------------------------
// Seam A: decodeEcuFrame + EcuState
// ---------------------------------------------------------------------------

// Issue #33 reference case: 70 17 00 00 00 00 E8 03 -> 6000 rpm, lambda 1.000.
static void test_reference_frame_0x520() {
    const uint8_t data[8] = {0x70, 0x17, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x03};
    EcuState state;

    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(frame(0x520, data), state));

    TEST_ASSERT_EQUAL_INT32(6000, state.raw(EcuSignal::EngineSpeed));
    TEST_ASSERT_EQUAL_INT32(1000, state.raw(EcuSignal::LambdaAverage));
    TEST_ASSERT_EQUAL_DOUBLE(6000.0, state.value(EcuSignal::EngineSpeed));
    TEST_ASSERT_EQUAL_DOUBLE(1.000, state.value(EcuSignal::LambdaAverage));
}

// Receipt is per message: 0x520 says nothing about the other six.
static void test_accepted_frame_marks_only_its_message_received() {
    const uint8_t data[8] = {0x70, 0x17, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x03};
    EcuState state;

    decodeEcuFrame(frame(0x520, data, 4321), state);

    TEST_ASSERT_TRUE(state.message(EcuMessage::Engine1).received);
    TEST_ASSERT_EQUAL_UINT32(4321, state.message(EcuMessage::Engine1).last_rx_ms);
    for (size_t m = 1; m < kEcuMessageCount; ++m) {
        TEST_ASSERT_FALSE(state.messages[m].received);
        TEST_ASSERT_EQUAL_UINT32(0, state.messages[m].last_rx_ms);
    }
}

// Issue check: target-lambda bytes 18 FC at bytes 6-7 -> -1.000 (signed i16).
static void test_lambda_target_is_signed() {
    const uint8_t data[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0xFC};
    EcuState state;

    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(frame(0x527, data), state));

    TEST_ASSERT_EQUAL_INT32(-1000, state.raw(EcuSignal::LambdaTarget));
    TEST_ASSERT_EQUAL_DOUBLE(-1.000, state.value(EcuSignal::LambdaTarget));
}

// --- Rejected frames change nothing ----------------------------------------

static void assertSameState(const EcuState& expected, const EcuState& actual) {
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected.raw_values, actual.raw_values, kEcuSignalCount);
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        TEST_ASSERT_EQUAL(expected.messages[m].received, actual.messages[m].received);
        TEST_ASSERT_EQUAL_UINT32(expected.messages[m].last_rx_ms, actual.messages[m].last_rx_ms);
    }
}

// A state holding a good 0x520 (6000 rpm, lambda 1.000) received at 1000 ms.
static EcuState primedState() {
    static const uint8_t good[8] = {0x70, 0x17, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x03};
    EcuState state;
    decodeEcuFrame(frame(0x520, good, 1000), state);
    return state;
}

// A different, otherwise-plausible 0x520 payload (60000 rpm) arriving later.
static const uint8_t kOtherPayload[16] = {0x60, 0xEA, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                                          0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

static void assertRejected(DecodeResult expected_result, const CanFrame& bad) {
    EcuState state = primedState();
    const EcuState before = state;
    TEST_ASSERT_EQUAL(expected_result, decodeEcuFrame(bad, state));
    assertSameState(before, state);
}

static void test_short_frame_is_rejected_without_change() {
    for (uint8_t dlc = 0; dlc < 8; ++dlc) {
        CanFrame f = frame(0x520, kOtherPayload, 2000);
        f.dlc = dlc;
        assertRejected(DecodeResult::BadLength, f);
    }
}

// The ESP32 driver can report a length code above 8 (non-compliant frame).
static void test_long_frame_is_rejected_without_change() {
    for (uint8_t dlc = 9; dlc <= 15; ++dlc) {
        CanFrame f = frame(0x520, kOtherPayload, 2000);
        f.dlc = dlc;
        assertRejected(DecodeResult::BadLength, f);
    }
}

static void test_extended_frame_with_our_id_is_rejected_without_change() {
    CanFrame f = frame(0x520, kOtherPayload, 2000);
    f.extended = true;
    assertRejected(DecodeResult::Extended, f);
}

static void test_remote_request_is_rejected_without_change() {
    CanFrame f = frame(0x520, kOtherPayload, 2000);
    f.remote = true;
    assertRejected(DecodeResult::Remote, f);
}

static void test_null_payload_is_rejected_without_change() {
    CanFrame f = frame(0x520, nullptr, 2000);
    assertRejected(DecodeResult::NullPayload, f);
}

// Other devices share the bus (aero probe, IMU...); excluded ECU IDs too.
static void test_unknown_ids_are_ignored_without_change() {
    const uint32_t others[] = {0x000, 0x51F, 0x523, 0x524, 0x525, 0x528, 0x531, 0x535, 0x537, 0x7FF};
    for (uint32_t id : others) {
        assertRejected(DecodeResult::UnknownId, frame(id, kOtherPayload, 2000));
    }
}

// --- Every signal: a representative value and the raw limits --------------
// Bytes are written exactly as they appear on the bus (byte 0 first).

static EcuState decodeOne(uint32_t id, const uint8_t* data) {
    EcuState state;
    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(frame(id, data), state));
    return state;
}

static void assertSignal(const EcuState& state, EcuSignal sig, int32_t raw, double physical) {
    TEST_ASSERT_EQUAL_INT32(raw, state.raw(sig));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, physical, state.value(sig));
}

// Issue check: RPM bytes 60 EA -> 60000 rpm, not negative (GRC26 used int16).
static void test_engine_speed_above_32767_stays_positive() {
    const uint8_t data[8] = {0x60, 0xEA, 0, 0, 0, 0, 0, 0};
    assertSignal(decodeOne(0x520, data), EcuSignal::EngineSpeed, 60000, 60000.0);
}

// Little-endian: the FIRST byte is the low byte.
static void test_byte_order_low_byte_first() {
    const uint8_t low[8] = {0x01, 0x00, 0, 0, 0, 0, 0x01, 0x00};
    const EcuState a = decodeOne(0x520, low);
    assertSignal(a, EcuSignal::EngineSpeed, 1, 1.0);
    assertSignal(a, EcuSignal::LambdaAverage, 1, 0.001);

    const uint8_t high[8] = {0x00, 0x01, 0, 0, 0, 0, 0x00, 0x01};
    const EcuState b = decodeOne(0x520, high);
    assertSignal(b, EcuSignal::EngineSpeed, 256, 256.0);
    assertSignal(b, EcuSignal::LambdaAverage, 256, 0.256);
}

static void test_engine_1_limits_and_excluded_throttle_bytes() {
    const uint8_t max[8] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF};
    const EcuState a = decodeOne(0x520, max);
    assertSignal(a, EcuSignal::EngineSpeed, 65535, 65535.0);
    assertSignal(a, EcuSignal::LambdaAverage, 65535, 65.535);

    // Bytes 2-5 (ThrottlePos, MAP) all ones must not leak into our signals.
    const uint8_t middle[8] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
    const EcuState b = decodeOne(0x520, middle);
    assertSignal(b, EcuSignal::EngineSpeed, 0, 0.0);
    assertSignal(b, EcuSignal::LambdaAverage, 0, 0.0);
}

static void test_lambda_a_and_b() {
    // A = E8 03 = 1000 -> 1.000; B = 52 03 = 850 -> 0.850; bytes 4-7 junk.
    const uint8_t data[8] = {0xE8, 0x03, 0x52, 0x03, 0xFF, 0xFF, 0xFF, 0xFF};
    const EcuState a = decodeOne(0x521, data);
    assertSignal(a, EcuSignal::LambdaA, 1000, 1.000);
    assertSignal(a, EcuSignal::LambdaB, 850, 0.850);

    // Limits; B = 00 80 = 32768 is unsigned, so +32.768 (not negative).
    const uint8_t lim[8] = {0xFF, 0xFF, 0x00, 0x80, 0, 0, 0, 0};
    const EcuState b = decodeOne(0x521, lim);
    assertSignal(b, EcuSignal::LambdaA, 65535, 65.535);
    assertSignal(b, EcuSignal::LambdaB, 32768, 32.768);
}

static void test_fuel_pulsewidth_and_duty() {
    // PW = 4C 04 = 1100 -> 11.00 ms; duty = 52 03 = 850 -> 85.0 %;
    // bytes 6-7 (VehicleSpeed, excluded) junk.
    const uint8_t data[8] = {0x4C, 0x04, 0x52, 0x03, 0x00, 0x00, 0xFF, 0xFF};
    const EcuState a = decodeOne(0x522, data);
    assertSignal(a, EcuSignal::FuelInjPulsewidth, 1100, 11.00);
    assertSignal(a, EcuSignal::FuelInjDuty, 850, 85.0);

    const uint8_t max[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    const EcuState b = decodeOne(0x522, max);
    assertSignal(b, EcuSignal::FuelInjPulsewidth, 65535, 655.35);
    assertSignal(b, EcuSignal::FuelInjDuty, 65535, 6553.5);
}

// Issue check: 0x526 byte 0 = 80 -> knock true. Knock is bit 7 only.
static void test_knock_is_bit_7_of_byte_0() {
    const uint8_t knock[8] = {0x80, 0, 0, 0, 0, 0, 0, 0};
    assertSignal(decodeOne(0x526, knock), EcuSignal::KnockDetected, 1, 1.0);

    const uint8_t other_bits[8] = {0x7F, 0, 0, 0, 0, 0, 0, 0};
    assertSignal(decodeOne(0x526, other_bits), EcuSignal::KnockDetected, 0, 0.0);

    // Brake (bit 8) and clutch (bit 9) set: not knock, and not exported.
    const uint8_t pedals[8] = {0x00, 0x03, 0, 0, 0, 0, 0, 0};
    const EcuState p = decodeOne(0x526, pedals);
    assertSignal(p, EcuSignal::KnockDetected, 0, 0.0);
    assertSignal(p, EcuSignal::RevLimitRpm, 0, 0.0);
}

static void test_rev_limit() {
    // F8 2A = 0x2AF8 = 11000 rpm, in bytes 4-5.
    const uint8_t data[8] = {0, 0, 0, 0, 0xF8, 0x2A, 0, 0};
    assertSignal(decodeOne(0x526, data), EcuSignal::RevLimitRpm, 11000, 11000.0);

    const uint8_t max[8] = {0, 0, 0, 0, 0xFF, 0xFF, 0, 0};
    assertSignal(decodeOne(0x526, max), EcuSignal::RevLimitRpm, 65535, 65535.0);
}

static void test_lambda_target_signed_limits() {
    const uint8_t plus_one[8] = {0, 0, 0, 0, 0, 0, 0xE8, 0x03};
    assertSignal(decodeOne(0x527, plus_one), EcuSignal::LambdaTarget, 1000, 1.000);

    const uint8_t most_negative[8] = {0, 0, 0, 0, 0, 0, 0x00, 0x80};
    assertSignal(decodeOne(0x527, most_negative), EcuSignal::LambdaTarget, -32768, -32.768);

    const uint8_t most_positive[8] = {0, 0, 0, 0, 0, 0, 0xFF, 0x7F};
    assertSignal(decodeOne(0x527, most_positive), EcuSignal::LambdaTarget, 32767, 32.767);

    const uint8_t minus_one_raw[8] = {0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    assertSignal(decodeOne(0x527, minus_one_raw), EcuSignal::LambdaTarget, -1, -0.001);

    // Bytes 0-5 (accelerations in the MaxxECU DBC) must not leak in.
    const uint8_t junk_before[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
    assertSignal(decodeOne(0x527, junk_before), EcuSignal::LambdaTarget, 0, 0.0);
}

static void test_environment_values() {
    // Battery 2E 05 = 1326 -> 13.26 V; bytes 2-3 (BaroPress, excluded) junk;
    // IAT 5A 01 = 346 -> 34.6 C; coolant 75 03 = 885 -> 88.5 C.
    const uint8_t data[8] = {0x2E, 0x05, 0xFF, 0xFF, 0x5A, 0x01, 0x75, 0x03};
    const EcuState a = decodeOne(0x530, data);
    assertSignal(a, EcuSignal::BatteryVoltage, 1326, 13.26);
    assertSignal(a, EcuSignal::IntakeAirTemp, 346, 34.6);
    assertSignal(a, EcuSignal::CoolantTemp, 885, 88.5);
}

// The DBC declares the temperatures UNSIGNED: FF FF is 6553.5 C, not -0.1 C.
// (If the ECU ever sends sub-zero temps this way, the DBC must change first.)
static void test_temperatures_are_unsigned() {
    const uint8_t max[8] = {0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    const EcuState a = decodeOne(0x530, max);
    assertSignal(a, EcuSignal::BatteryVoltage, 65535, 655.35);
    assertSignal(a, EcuSignal::IntakeAirTemp, 65535, 6553.5);
    assertSignal(a, EcuSignal::CoolantTemp, 65535, 6553.5);

    const uint8_t top_bit[8] = {0, 0, 0, 0, 0x00, 0x80, 0x00, 0x80};
    const EcuState b = decodeOne(0x530, top_bit);
    assertSignal(b, EcuSignal::IntakeAirTemp, 32768, 3276.8);
    assertSignal(b, EcuSignal::CoolantTemp, 32768, 3276.8);
}

// Gear is a raw code (the ECU's own numbering), kept exactly.
static void test_gear_raw_code() {
    const uint8_t third[8] = {0x03, 0x00, 0, 0, 0, 0, 0, 0};
    assertSignal(decodeOne(0x536, third), EcuSignal::GearPosn, 3, 3.0);

    const uint8_t high[8] = {0x00, 0x01, 0, 0, 0, 0, 0, 0};
    assertSignal(decodeOne(0x536, high), EcuSignal::GearPosn, 256, 256.0);

    const uint8_t max[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    assertSignal(decodeOne(0x536, max), EcuSignal::GearPosn, 65535, 65535.0);
}

// Issue check: a valid all-zero frame gives RECEIVED zero values (a real
// zero), which is different from "never received".
static void test_all_zero_payload_gives_received_zeros() {
    const uint8_t zeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    // Start from all-ones values so a "did nothing" decoder can't pass.
    EcuState state;
    for (size_t i = 0; i < kEcuSignalCount; ++i) {
        state.raw_values[i] = 12345;
    }
    for (const EcuMessageDef& msg : kEcuMessages) {
        TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(frame(msg.id, zeros, 777), state));
    }
    for (size_t i = 0; i < kEcuSignalCount; ++i) {
        TEST_ASSERT_EQUAL_INT32(0, state.raw_values[i]);
    }
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        TEST_ASSERT_TRUE(state.messages[m].received);
        TEST_ASSERT_EQUAL_UINT32(777, state.messages[m].last_rx_ms);
    }
}

// A frame only updates its own signals; the other messages' values stay.
static void test_frame_updates_only_its_own_signals() {
    const uint8_t env[8] = {0x2E, 0x05, 0, 0, 0x5A, 0x01, 0x75, 0x03};
    const uint8_t rpm[8] = {0x70, 0x17, 0, 0, 0, 0, 0xE8, 0x03};
    EcuState state;
    decodeEcuFrame(frame(0x530, env, 100), state);
    decodeEcuFrame(frame(0x520, rpm, 200), state);

    assertSignal(state, EcuSignal::BatteryVoltage, 1326, 13.26);
    assertSignal(state, EcuSignal::CoolantTemp, 885, 88.5);
    TEST_ASSERT_EQUAL_UINT32(100, state.message(EcuMessage::Environment).last_rx_ms);
    TEST_ASSERT_EQUAL_UINT32(200, state.message(EcuMessage::Engine1).last_rx_ms);
}

// The issue's list: exactly these 13 signals, in these 7 messages.
static void test_exactly_the_selected_signals_are_decoded() {
    const char* expected[] = {"EngineSpeed",        "Lambda_Average", "Lambda_A",     "Lambda_B",
                              "FuelInjPulsewidth",  "FuelInjDuty",    "Knock_detected", "RevLimit_RPM",
                              "Lambda_Target",      "ECU_BatteryVoltage", "IntakeAirTemp", "CoolantTemp",
                              "GearPosn"};
    TEST_ASSERT_EQUAL_size_t(13, kEcuSignalCount);
    for (size_t i = 0; i < kEcuSignalCount; ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i], kEcuSignals[i].name);
    }
    const uint32_t ids[] = {0x520, 0x521, 0x522, 0x526, 0x527, 0x530, 0x536};
    TEST_ASSERT_EQUAL_size_t(7, kEcuMessageCount);
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        TEST_ASSERT_EQUAL_HEX32(ids[m], kEcuMessages[m].id);
    }
}

// --- Cross-check against the real DBC file --------------------------------
// Reads GRC-27/Telemetry/GRC27.dbc (found next to this test file) and checks
// every row of kEcuSignals: ID, start bit, length, little-endian, sign,
// scale, zero offset. A DBC edit not copied into EcuSignals.h fails here.

struct DbcSignal {
    uint32_t message_id;
    unsigned start, length;
    char byte_order; // '1' = little-endian (Intel)
    char sign;       // '+' unsigned, '-' signed
    double scale, offset;
};

static FILE* openDbc() {
    // This file is GRC-27/Dyno/test/test_can/test_can.cpp.
    static const char kFromHere[] = "/../../../Telemetry/GRC27.dbc";
    char path[1024];
    const char* here = __FILE__;
    const char* slash = strrchr(here, '/');
    if (slash != nullptr && here[0] == '/') {
        snprintf(path, sizeof(path), "%.*s%s", static_cast<int>(slash - here), here, kFromHere);
        if (FILE* f = fopen(path, "r")) return f;
    }
    // Fallback: tests run from GRC-27/Dyno/.
    return fopen("../Telemetry/GRC27.dbc", "r");
}

// Finds `name` in the DBC; false if it isn't there.
static bool findDbcSignal(const char* name, DbcSignal& out) {
    FILE* f = openDbc();
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "could not open Telemetry/GRC27.dbc");
    char line[512];
    uint32_t current_id = 0;
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) {
        unsigned long id;
        char sg_name[128];
        DbcSignal sig;
        if (sscanf(line, "BO_ %lu", &id) == 1) {
            current_id = static_cast<uint32_t>(id);
        } else if (sscanf(line, " SG_ %127s : %u|%u@%c%c (%lf,%lf)", sg_name, &sig.start, &sig.length,
                          &sig.byte_order, &sig.sign, &sig.scale, &sig.offset) == 7 &&
                   strcmp(sg_name, name) == 0) {
            sig.message_id = current_id;
            out = sig;
            found = true;
        }
    }
    fclose(f);
    return found;
}

static void test_signal_table_matches_grc27_dbc() {
    for (const EcuSignalDef& row : kEcuSignals) {
        DbcSignal dbc;
        TEST_ASSERT_TRUE_MESSAGE(findDbcSignal(row.name, dbc), row.name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(dbc.message_id, ecuMessageDef(row.message).id, row.name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(dbc.start, row.start_bit, row.name);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(dbc.length, row.length, row.name);
        TEST_ASSERT_EQUAL_CHAR_MESSAGE('1', dbc.byte_order, row.name); // decoder is little-endian only
        TEST_ASSERT_EQUAL_MESSAGE(dbc.sign == '-', row.is_signed, row.name);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1e-12, dbc.scale, row.scale, row.name);
        TEST_ASSERT_EQUAL_DOUBLE_MESSAGE(0.0, dbc.offset, row.name); // decoder assumes zero offset
    }
}

// Signals the issue says NOT to decode: present in the DBC, absent from us.
static void test_excluded_signals_are_absent() {
    const char* excluded[] = {"ThrottlePos", "VehicleSpeed", "Brake_pedal_active", "Clutch_pedal_active",
                              "User_Channel_1"};
    for (const char* name : excluded) {
        DbcSignal dbc;
        TEST_ASSERT_TRUE_MESSAGE(findDbcSignal(name, dbc), name); // still in the DBC
        for (const EcuSignalDef& row : kEcuSignals) {
            TEST_ASSERT_FALSE_MESSAGE(strcmp(row.name, name) == 0, name);
        }
    }
}

// ---------------------------------------------------------------------------
// Seam B: received vs fresh
//   received = a valid frame of that message has EVER arrived since boot
//   fresh    = the latest one arrived less than `freshness_ms` ago
// Mask bit order: 0=0x520 1=0x521 2=0x522 3=0x526 4=0x527 5=0x530 6=0x536
// ---------------------------------------------------------------------------

static const uint32_t kFreshMs = 2000; // DynoConfig default (team convention)
static const uint8_t kZeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};

static void receive(EcuState& state, uint32_t id, uint32_t at_ms) {
    TEST_ASSERT_EQUAL(DecodeResult::Accepted, decodeEcuFrame(frame(id, kZeros, at_ms), state));
}

static void test_nothing_received_at_startup() {
    EcuState state;
    TEST_ASSERT_EQUAL_HEX8(0x00, state.receivedMask());
    TEST_ASSERT_EQUAL_HEX8(0x00, state.freshMask(0, kFreshMs));
    TEST_ASSERT_EQUAL_HEX8(0x00, state.freshMask(100000, kFreshMs));
}

static void test_mask_bits_follow_message_order() {
    EcuState state;
    receive(state, 0x520, 1000); // bit 0
    receive(state, 0x530, 1000); // bit 5
    TEST_ASSERT_EQUAL_HEX8(0x21, state.receivedMask());
    receive(state, 0x521, 1000); // bit 1
    receive(state, 0x522, 1000); // bit 2
    receive(state, 0x526, 1000); // bit 3
    receive(state, 0x527, 1000); // bit 4
    receive(state, 0x536, 1000); // bit 6
    TEST_ASSERT_EQUAL_HEX8(0x7F, state.receivedMask());
    TEST_ASSERT_EQUAL_HEX8(0x7F, state.freshMask(1000, kFreshMs));
}

// Fresh while age < freshness; stale from age == freshness onwards.
static void test_freshness_boundary() {
    EcuState state;
    receive(state, 0x520, 1000);
    TEST_ASSERT_EQUAL_HEX8(0x01, state.freshMask(1000, kFreshMs));
    TEST_ASSERT_EQUAL_HEX8(0x01, state.freshMask(2999, kFreshMs));
    TEST_ASSERT_EQUAL_HEX8(0x00, state.freshMask(3000, kFreshMs));
    TEST_ASSERT_EQUAL_HEX8(0x01, state.receivedMask()); // still "received"
    TEST_ASSERT_TRUE(state.isFresh(EcuMessage::Engine1, 2999, kFreshMs));
    TEST_ASSERT_FALSE(state.isFresh(EcuMessage::Engine1, 3000, kFreshMs));
    TEST_ASSERT_FALSE(state.isFresh(EcuMessage::Gear, 1000, kFreshMs)); // never received
}

// Issue check: only 0x520 keeps arriving -> the others expire on their own.
static void test_only_0x520_keeps_arriving() {
    EcuState state;
    for (const EcuMessageDef& msg : kEcuMessages) {
        receive(state, msg.id, 0);
    }
    for (uint32_t t = 20; t <= 5000; t += 20) {
        receive(state, 0x520, t);
        // The other six were last seen at 0 ms: fresh until 2000, then stale.
        TEST_ASSERT_EQUAL_HEX8(t < kFreshMs ? 0x7F : 0x01, state.freshMask(t, kFreshMs));
    }
    TEST_ASSERT_EQUAL_HEX8(0x7F, state.receivedMask());
    TEST_ASSERT_EQUAL_UINT32(0, state.message(EcuMessage::Environment).last_rx_ms);
}

// millis() wraps from 4294967295 back to 0 after ~49.7 days.
static void test_freshness_across_millis_rollover() {
    EcuState state;
    receive(state, 0x520, 0xFFFFFF00u); // 256 ms before the wrap
    TEST_ASSERT_EQUAL_HEX8(0x01, state.freshMask(0xFFFFFFFFu, kFreshMs));
    TEST_ASSERT_EQUAL_HEX8(0x01, state.freshMask(0, kFreshMs));        // age 256
    TEST_ASSERT_EQUAL_HEX8(0x01, state.freshMask(1743, kFreshMs));     // age 1999
    TEST_ASSERT_EQUAL_HEX8(0x00, state.freshMask(1744, kFreshMs));     // age 2000
    TEST_ASSERT_EQUAL_HEX8(0x00, state.freshMask(0x7FFFFFFFu, kFreshMs));
}

static void test_fresh_again_after_new_frame_post_rollover() {
    EcuState state;
    receive(state, 0x530, 0xFFFFF000u);
    TEST_ASSERT_EQUAL_HEX8(0x00, state.freshMask(5000, kFreshMs)); // stale across the wrap
    receive(state, 0x530, 5000);
    TEST_ASSERT_EQUAL_HEX8(0x20, state.freshMask(6999, kFreshMs));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reference_frame_0x520);
    RUN_TEST(test_accepted_frame_marks_only_its_message_received);
    RUN_TEST(test_lambda_target_is_signed);
    RUN_TEST(test_short_frame_is_rejected_without_change);
    RUN_TEST(test_long_frame_is_rejected_without_change);
    RUN_TEST(test_extended_frame_with_our_id_is_rejected_without_change);
    RUN_TEST(test_remote_request_is_rejected_without_change);
    RUN_TEST(test_null_payload_is_rejected_without_change);
    RUN_TEST(test_unknown_ids_are_ignored_without_change);
    RUN_TEST(test_engine_speed_above_32767_stays_positive);
    RUN_TEST(test_byte_order_low_byte_first);
    RUN_TEST(test_engine_1_limits_and_excluded_throttle_bytes);
    RUN_TEST(test_lambda_a_and_b);
    RUN_TEST(test_fuel_pulsewidth_and_duty);
    RUN_TEST(test_knock_is_bit_7_of_byte_0);
    RUN_TEST(test_rev_limit);
    RUN_TEST(test_lambda_target_signed_limits);
    RUN_TEST(test_environment_values);
    RUN_TEST(test_temperatures_are_unsigned);
    RUN_TEST(test_gear_raw_code);
    RUN_TEST(test_all_zero_payload_gives_received_zeros);
    RUN_TEST(test_frame_updates_only_its_own_signals);
    RUN_TEST(test_exactly_the_selected_signals_are_decoded);
    RUN_TEST(test_signal_table_matches_grc27_dbc);
    RUN_TEST(test_excluded_signals_are_absent);
    RUN_TEST(test_nothing_received_at_startup);
    RUN_TEST(test_mask_bits_follow_message_order);
    RUN_TEST(test_freshness_boundary);
    RUN_TEST(test_only_0x520_keeps_arriving);
    RUN_TEST(test_freshness_across_millis_rollover);
    RUN_TEST(test_fresh_again_after_new_frame_post_rollover);
    return UNITY_END();
}
