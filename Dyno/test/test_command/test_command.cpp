// Serial command input (#35 step 3): the bounded line reader and the command
// parser, tested with no serial port.
#include <math.h>
#include <string.h>

#include <string>
#include <unity.h>

#include "CommandParser.h"
#include "LineReader.h"

void setUp() {}
void tearDown() {}

// Pushes every byte of `text`; returns the events in order, as letters:
// L = a line (its text appended after ':'), T = too long, B = bad byte.
static std::string pushAll(LineReader& r, const char* text) {
    std::string events;
    for (const char* p = text; *p; ++p) {
        switch (r.push(*p)) {
        case LineEvent::None:
            break;
        case LineEvent::Line:
            events += "L:";
            events += r.line();
            events += ";";
            break;
        case LineEvent::TooLong:
            events += "T;";
            break;
        case LineEvent::BadByte:
            events += "B;";
            break;
        }
    }
    return events;
}

// ---------------------------------------------------------------------------
// LineReader

// Serial monitors end lines with LF, CR or CRLF; all give one line, and the
// blank "line" between CR and LF is not reported.
static void test_lf_cr_and_crlf_each_end_one_line() {
    LineReader r;
    TEST_ASSERT_EQUAL_STRING("L:tare;", pushAll(r, "tare\n").c_str());
    TEST_ASSERT_EQUAL_STRING("L:tare;", pushAll(r, "tare\r").c_str());
    TEST_ASSERT_EQUAL_STRING("L:tare;", pushAll(r, "tare\r\n").c_str());
    TEST_ASSERT_EQUAL_STRING("L:a;L:b;", pushAll(r, "a\r\n\r\n\nb\n").c_str());
}

// 64 characters is the longest line; a longer one is thrown away up to its
// line end and reported once, there. The reader then works normally.
static void test_an_over_long_line_is_reported_once_and_skipped() {
    LineReader r;
    const std::string fits(64, 'a');
    TEST_ASSERT_EQUAL_STRING(("L:" + fits + ";").c_str(), pushAll(r, (fits + "\n").c_str()).c_str());

    const std::string too_long(65, 'b');
    TEST_ASSERT_EQUAL_STRING("T;", pushAll(r, (too_long + "\n").c_str()).c_str());
    TEST_ASSERT_EQUAL_STRING("T;L:tare;", pushAll(r, (std::string(300, 'c') + "\r\ntare\n").c_str()).c_str());
}

// Control characters and non-ASCII bytes (line noise, a wrong baud rate)
// spoil the line: it is reported as bad at its end, never parsed.
static void test_a_line_with_a_bad_byte_is_rejected_whole() {
    LineReader r;
    TEST_ASSERT_EQUAL_STRING("B;", pushAll(r, "ta\x01re\n").c_str());
    TEST_ASSERT_EQUAL_STRING("B;", pushAll(r, "tare\xff\n").c_str());
    TEST_ASSERT_EQUAL_STRING("B;L:tare;", pushAll(r, "\x7f\ntare\n").c_str());
    // Tabs are fine (the parser treats them as spaces).
    TEST_ASSERT_EQUAL_STRING("L:cal\tstatus;", pushAll(r, "cal\tstatus\n").c_str());
    // Too long wins over bad: one report per line.
    TEST_ASSERT_EQUAL_STRING("T;", pushAll(r, (std::string(70, 'x') + "\x01\n").c_str()).c_str());
}

// ---------------------------------------------------------------------------
// CommandParser

static void test_cal_loaded_in_newtons() {
    const ParseResult p = parseCommand("cal loaded 100N");
    TEST_ASSERT_EQUAL_STRING("None", parseErrorName(p.error));
    TEST_ASSERT_TRUE(p.command.kind == CommandKind::CalLoaded);
    TEST_ASSERT_EQUAL_DOUBLE(100.0, p.command.reference_force_n);
    TEST_ASSERT_TRUE(isnan(p.command.reference_mass_kg)); // given as a force, not a mass
}

// 10.2 kg x 9.80665 m/s^2 = 100.02783 N (worked by hand). The mass typed is
// kept too. The unit may be attached or a separate word, in any case.
static void test_cal_loaded_in_kilograms() {
    const char* forms[] = {"cal loaded 10.2kg", "cal loaded 10.2 kg", "CAL LOADED 10.2 KG", "  cal\tloaded  10.2Kg  "};
    for (const char* line : forms) {
        const ParseResult p = parseCommand(line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("None", parseErrorName(p.error), line);
        TEST_ASSERT_TRUE_MESSAGE(p.command.kind == CommandKind::CalLoaded, line);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1e-9, 100.02783, p.command.reference_force_n, line);
        TEST_ASSERT_EQUAL_DOUBLE_MESSAGE(10.2, p.command.reference_mass_kg, line);
    }
}

static void test_number_forms_that_are_accepted() {
    TEST_ASSERT_EQUAL_DOUBLE(100.0, parseCommand("cal loaded 100 n").command.reference_force_n);
    TEST_ASSERT_EQUAL_DOUBLE(0.5, parseCommand("cal loaded .5N").command.reference_force_n);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, parseCommand("cal loaded 5.N").command.reference_force_n);
    TEST_ASSERT_EQUAL_DOUBLE(250.75, parseCommand("cal loaded 0250.75N").command.reference_force_n);
}

static void test_the_simple_commands() {
    TEST_ASSERT_TRUE(parseCommand("tare").command.kind == CommandKind::Tare);
    TEST_ASSERT_TRUE(parseCommand("  TARE  ").command.kind == CommandKind::Tare);
    TEST_ASSERT_TRUE(parseCommand("cal unloaded").command.kind == CommandKind::CalUnloaded);
    TEST_ASSERT_TRUE(parseCommand("cal cancel").command.kind == CommandKind::CalCancel);
    TEST_ASSERT_TRUE(parseCommand("cal\tstatus").command.kind == CommandKind::CalStatus);
    TEST_ASSERT_TRUE(isnan(parseCommand("tare").command.reference_force_n));
}

// Every rejected line names why, and carries no command.
static void test_rejected_lines() {
    struct Case {
        const char* line;
        const char* error;
    };
    const Case cases[] = {
        {"", "Empty"},
        {" \t ", "Empty"},
        {"hello", "UnknownCommand"},
        {"cal", "UnknownCommand"},
        {"cal foo", "UnknownCommand"},
        {"calibrate", "UnknownCommand"},
        {"tare now", "ExtraWords"},
        {"cal status x", "ExtraWords"},
        {"cal unloaded 100N", "ExtraWords"},
        {"cal loaded 100N extra", "ExtraWords"},
        {"cal loaded 100 N extra", "ExtraWords"},
        {"tare a b c d e", "ExtraWords"},
        {"cal loaded", "MissingValue"},
        {"cal loaded 100", "MissingUnit"},
        {"cal loaded 100 lb", "BadUnit"},
        {"cal loaded 100nm", "BadUnit"},
        {"cal loaded 100 kgs", "BadUnit"},
        {"cal loaded -5N", "BadNumber"},
        {"cal loaded +5N", "BadNumber"},
        {"cal loaded abc", "BadNumber"},
        {"cal loaded .N", "BadNumber"},
        {"cal loaded 1.2.3N", "BadNumber"},
        {"cal loaded inf N", "BadNumber"},
        {"cal loaded nan kg", "BadNumber"},
        {"cal loaded 0N", "NotPositive"},
        {"cal loaded 0.000 kg", "NotPositive"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "UnknownCommand"}, // one 41-char word
    };
    for (const Case& c : cases) {
        const ParseResult p = parseCommand(c.line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.error, parseErrorName(p.error), c.line);
        TEST_ASSERT_TRUE_MESSAGE(p.command.kind == CommandKind::None, c.line);
    }
}

static void test_parse_error_names() {
    TEST_ASSERT_EQUAL_STRING("NotPositive", parseErrorName(ParseError::NotPositive));
    TEST_ASSERT_EQUAL_STRING("MissingUnit", parseErrorName(ParseError::MissingUnit));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_lf_cr_and_crlf_each_end_one_line);
    RUN_TEST(test_an_over_long_line_is_reported_once_and_skipped);
    RUN_TEST(test_a_line_with_a_bad_byte_is_rejected_whole);
    RUN_TEST(test_cal_loaded_in_newtons);
    RUN_TEST(test_cal_loaded_in_kilograms);
    RUN_TEST(test_number_forms_that_are_accepted);
    RUN_TEST(test_the_simple_commands);
    RUN_TEST(test_rejected_lines);
    RUN_TEST(test_parse_error_names);
    return UNITY_END();
}
