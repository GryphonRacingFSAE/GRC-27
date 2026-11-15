#include "CommandParser.h"

#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "CalibrationMath.h"

namespace {

constexpr size_t kMaxWords = 4;
constexpr size_t kMaxWordLen = 32;

struct Words {
    char word[kMaxWords][kMaxWordLen + 1] = {};
    size_t count = 0;
    bool overflow = false; // too many words, or one too long to be any command
};

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Split on spaces/tabs, lower-casing as we go.
Words split(const char* line) {
    Words w;
    const char* p = line;
    while (*p) {
        while (isSpace(*p)) ++p;
        if (!*p) break;
        if (w.count == kMaxWords) {
            w.overflow = true;
            return w;
        }
        size_t n = 0;
        while (*p && !isSpace(*p)) {
            if (n == kMaxWordLen) {
                w.overflow = true;
                return w;
            }
            w.word[w.count][n++] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
            ++p;
        }
        w.word[w.count][n] = '\0';
        ++w.count;
    }
    return w;
}

ParseResult fail(ParseError e) {
    ParseResult r;
    r.error = e;
    return r;
}

ParseResult ok(CommandKind kind) {
    ParseResult r;
    r.command.kind = kind;
    return r;
}

// "<digits>[.<digits>]" with at least one digit; returns chars used, 0 if none.
size_t numberLength(const char* s) {
    size_t i = 0;
    size_t digits = 0;
    while (isdigit(static_cast<unsigned char>(s[i]))) {
        ++i;
        ++digits;
    }
    if (s[i] == '.') {
        ++i;
        while (isdigit(static_cast<unsigned char>(s[i]))) {
            ++i;
            ++digits;
        }
    }
    return digits ? i : 0;
}

// The value and unit: either one word ("100n") or two ("100", "n").
ParseResult parseLoaded(const Words& w) {
    if (w.count < 3) {
        return fail(ParseError::MissingValue);
    }
    const char* value = w.word[2];
    const size_t len = numberLength(value);
    if (len == 0) {
        return fail(ParseError::BadNumber);
    }
    const char* unit = value + len;
    if (*unit == '\0') {
        if (w.count == 3) {
            return fail(ParseError::MissingUnit);
        }
        unit = w.word[3];
    } else if (w.count > 3) {
        return fail(ParseError::ExtraWords);
    }
    if (isdigit(static_cast<unsigned char>(*unit)) || *unit == '.') {
        return fail(ParseError::BadNumber); // e.g. "1.2.3n"
    }

    char number[kMaxWordLen + 1];
    memcpy(number, value, len);
    number[len] = '\0';
    const double x = strtod(number, nullptr);
    if (!(x > 0.0) || !isfinite(x)) {
        return fail(ParseError::NotPositive);
    }

    ParseResult r = ok(CommandKind::CalLoaded);
    if (strcmp(unit, "n") == 0) {
        r.command.reference_force_n = x;
    } else if (strcmp(unit, "kg") == 0) {
        r.command.reference_mass_kg = x;
        r.command.reference_force_n = newtonsFromKilograms(x, kStandardGravityMs2);
    } else {
        return fail(ParseError::BadUnit);
    }
    return r;
}

} // namespace

ParseResult parseCommand(const char* line) {
    const Words w = split(line);
    if (w.count == 0 && !w.overflow) {
        return fail(ParseError::Empty);
    }
    if (w.overflow) {
        // Only "cal loaded <value> <unit>" has four words; anything longer
        // can't be a command, but say "extra words" if it starts like one.
        const bool known = strcmp(w.word[0], "tare") == 0 || strcmp(w.word[0], "cal") == 0;
        return fail(known ? ParseError::ExtraWords : ParseError::UnknownCommand);
    }

    if (strcmp(w.word[0], "tare") == 0) {
        return w.count == 1 ? ok(CommandKind::Tare) : fail(ParseError::ExtraWords);
    }
    if (strcmp(w.word[0], "cal") != 0 || w.count < 2) {
        return fail(ParseError::UnknownCommand);
    }

    const char* sub = w.word[1];
    if (strcmp(sub, "loaded") == 0) {
        return parseLoaded(w);
    }
    CommandKind kind = CommandKind::None;
    if (strcmp(sub, "unloaded") == 0) {
        kind = CommandKind::CalUnloaded;
    } else if (strcmp(sub, "cancel") == 0) {
        kind = CommandKind::CalCancel;
    } else if (strcmp(sub, "status") == 0) {
        kind = CommandKind::CalStatus;
    } else {
        return fail(ParseError::UnknownCommand);
    }
    return w.count == 2 ? ok(kind) : fail(ParseError::ExtraWords);
}

const char* parseErrorName(ParseError error) {
    switch (error) {
    case ParseError::None:
        return "None";
    case ParseError::Empty:
        return "Empty";
    case ParseError::UnknownCommand:
        return "UnknownCommand";
    case ParseError::ExtraWords:
        return "ExtraWords";
    case ParseError::MissingValue:
        return "MissingValue";
    case ParseError::BadNumber:
        return "BadNumber";
    case ParseError::MissingUnit:
        return "MissingUnit";
    case ParseError::BadUnit:
        return "BadUnit";
    case ParseError::NotPositive:
        return "NotPositive";
    }
    return "?";
}
