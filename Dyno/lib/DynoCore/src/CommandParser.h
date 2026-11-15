// Turns one typed line into a load-cell command (#35). Pure: no serial, no
// state. The grammar (words are case-insensitive; spaces and tabs separate):
//
//   tare                     zero the reading with nothing on the rig
//   cal unloaded             record the unloaded reading
//   cal loaded <value><unit> record the loaded reading; unit N or kg,
//                            e.g. "cal loaded 100N", "cal loaded 10.2 kg"
//   cal cancel               abandon a calibration in progress
//   cal status               report the stored calibration
//
// A mass in kg is turned into newtons with standard gravity (9.80665 m/s^2);
// both numbers are kept so the record shows what was typed. The unit is
// required: a bare number is ambiguous, and guessing wrong would silently
// scale every torque reading by 9.8.
#pragma once

#include <math.h>
#include <stdint.h>

enum class CommandKind : uint8_t { None, Tare, CalUnloaded, CalLoaded, CalCancel, CalStatus };

enum class ParseError : uint8_t {
    None,
    Empty,          // only spaces
    UnknownCommand, // not one of the commands above
    ExtraWords,     // a known command followed by more words
    MissingValue,   // "cal loaded" with no value
    BadNumber,      // the value isn't a plain decimal number
    MissingUnit,    // a number with no N / kg
    BadUnit,        // a unit other than N / kg
    NotPositive,    // the value is zero
};

struct LoadCellCommand {
    CommandKind kind = CommandKind::None;
    double reference_force_n = NAN; // CalLoaded: the reference force (N)
    double reference_mass_kg = NAN; // CalLoaded given in kg: the mass typed
};

struct ParseResult {
    ParseError error = ParseError::None;
    LoadCellCommand command; // kind None unless error is None
};

ParseResult parseCommand(const char* line);

const char* parseErrorName(ParseError error);
