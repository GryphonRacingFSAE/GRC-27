// ============================================================================
// The ECU signals the dyno decodes: this file IS our copy of the DBC.
//
// Source: GRC-27/Telemetry/GRC27.dbc (last changed in commit 7faab7e,
// 2026-09-23). Also matches MaxxECU's own raw DBC. test_can checks every row
// against GRC27.dbc, so a DBC edit that isn't copied here fails the tests.
//
// To change a signal: edit its row in kEcuSignals. To add one: add it to
// EcuSignal (before Count) and add a row. To add a message: add it to
// EcuMessage and kEcuMessages. Nothing else needs to change.
//
// Every signal is little-endian (DBC "@1") with zero offset:
//   physical value = raw value x scale
// ============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// The messages we listen to. Order = bit order of the CSV received/fresh
// masks (bit 0 = Engine1 = 0x520), so don't reorder casually.
enum class EcuMessage : uint8_t {
    Engine1,      // 0x520
    Lambda,       // 0x521
    FuelSpeed,    // 0x522
    Status,       // 0x526
    LambdaTarget, // 0x527
    Environment,  // 0x530
    Gear,         // 0x536
    Count
};

enum class EcuSignal : uint8_t {
    EngineSpeed,
    LambdaAverage,
    LambdaA,
    LambdaB,
    FuelInjPulsewidth,
    FuelInjDuty,
    KnockDetected,
    RevLimitRpm,
    LambdaTarget,
    BatteryVoltage,
    IntakeAirTemp,
    CoolantTemp,
    GearPosn,
    Count
};

static constexpr size_t kEcuMessageCount = static_cast<size_t>(EcuMessage::Count);
static constexpr size_t kEcuSignalCount = static_cast<size_t>(EcuSignal::Count);

struct EcuMessageDef {
    EcuMessage message;
    uint32_t id;      // standard 11-bit CAN ID
    const char* name; // DBC message name
};

struct EcuSignalDef {
    EcuSignal signal;
    EcuMessage message;
    uint8_t start_bit; // DBC bit position (little-endian: bit 0 = LSB of byte 0)
    uint8_t length;    // in bits, 1..32
    bool is_signed;    // two's complement (DBC "-")
    double scale;      // physical = raw x scale
    const char* name;  // DBC signal name
    const char* unit;
};

// Rows must be in enum order (checked by the static_asserts below).
inline constexpr EcuMessageDef kEcuMessages[] = {
    {EcuMessage::Engine1,      0x520, "ECU_ENGINE_1"},
    {EcuMessage::Lambda,       0x521, "ECU_LAMBDA"},
    {EcuMessage::FuelSpeed,    0x522, "ECU_FUEL_SPEED"},
    {EcuMessage::Status,       0x526, "ECU_STATUS"},
    {EcuMessage::LambdaTarget, 0x527, "ECU_LAMBDA_TARGET"},
    {EcuMessage::Environment,  0x530, "ECU_ENVIRONMENT"},
    {EcuMessage::Gear,         0x536, "ECU_GEAR"},
};

inline constexpr EcuSignalDef kEcuSignals[] = {
    // signal                         message                   start len signed scale   DBC name              unit
    {EcuSignal::EngineSpeed,       EcuMessage::Engine1,      0,  16, false, 1,     "EngineSpeed",        "rpm"},
    {EcuSignal::LambdaAverage,     EcuMessage::Engine1,      48, 16, false, 0.001, "Lambda_Average",     "lambda"},
    {EcuSignal::LambdaA,           EcuMessage::Lambda,       0,  16, false, 0.001, "Lambda_A",           "lambda"},
    {EcuSignal::LambdaB,           EcuMessage::Lambda,       16, 16, false, 0.001, "Lambda_B",           "lambda"},
    {EcuSignal::FuelInjPulsewidth, EcuMessage::FuelSpeed,    0,  16, false, 0.01,  "FuelInjPulsewidth",  "ms"},
    {EcuSignal::FuelInjDuty,       EcuMessage::FuelSpeed,    16, 16, false, 0.1,   "FuelInjDuty",        "%"},
    {EcuSignal::KnockDetected,     EcuMessage::Status,       7,  1,  false, 1,     "Knock_detected",     ""},
    {EcuSignal::RevLimitRpm,       EcuMessage::Status,       32, 16, false, 1,     "RevLimit_RPM",       "rpm"},
    {EcuSignal::LambdaTarget,      EcuMessage::LambdaTarget, 48, 16, true,  0.001, "Lambda_Target",      "lambda"},
    {EcuSignal::BatteryVoltage,    EcuMessage::Environment,  0,  16, false, 0.01,  "ECU_BatteryVoltage", "V"},
    {EcuSignal::IntakeAirTemp,     EcuMessage::Environment,  32, 16, false, 0.1,   "IntakeAirTemp",      "degC"},
    {EcuSignal::CoolantTemp,       EcuMessage::Environment,  48, 16, false, 0.1,   "CoolantTemp",        "degC"},
    {EcuSignal::GearPosn,          EcuMessage::Gear,         0,  16, false, 1,     "GearPosn",           ""},
};

// ---- Table sanity, checked when compiling --------------------------------
constexpr bool ecuTablesAreConsistent() {
    if (sizeof(kEcuMessages) / sizeof(kEcuMessages[0]) != kEcuMessageCount) return false;
    if (sizeof(kEcuSignals) / sizeof(kEcuSignals[0]) != kEcuSignalCount) return false;
    for (size_t i = 0; i < kEcuMessageCount; ++i) {
        if (static_cast<size_t>(kEcuMessages[i].message) != i) return false;
        if (kEcuMessages[i].id > 0x7FF) return false; // must be a standard ID
        for (size_t j = i + 1; j < kEcuMessageCount; ++j) {
            if (kEcuMessages[i].id == kEcuMessages[j].id) return false;
        }
    }
    for (size_t i = 0; i < kEcuSignalCount; ++i) {
        const EcuSignalDef& s = kEcuSignals[i];
        if (static_cast<size_t>(s.signal) != i) return false;
        if (s.length < 1 || s.length > 32) return false;
        if (s.start_bit + s.length > 64) return false; // must fit in 8 bytes
        if (s.scale == 0) return false;
    }
    return true;
}
static_assert(ecuTablesAreConsistent(), "EcuSignals.h: tables out of enum order, duplicate ID, or a signal outside 8 bytes");
static_assert(kEcuMessageCount <= 8, "EcuSignals.h: CSV masks are 8 bits; more messages need a wider mask");

inline constexpr const EcuMessageDef& ecuMessageDef(EcuMessage m) {
    return kEcuMessages[static_cast<size_t>(m)];
}
inline constexpr const EcuSignalDef& ecuSignalDef(EcuSignal s) {
    return kEcuSignals[static_cast<size_t>(s)];
}

// The message with this standard ID, or nullptr if it isn't one of ours.
inline constexpr const EcuMessageDef* findEcuMessage(uint32_t id) {
    for (const EcuMessageDef& msg : kEcuMessages) {
        if (msg.id == id) {
            return &msg;
        }
    }
    return nullptr;
}
