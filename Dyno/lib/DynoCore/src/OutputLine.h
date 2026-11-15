// One complete serial line (a CSV row, the header, or a "# " diagnostic),
// ready to print. Fixed size, plain data: it is copied whole through the
// output queue (#38), so a queued line never changes after it was made.
#pragma once

#include <stddef.h>
#include <stdint.h>

struct OutputLine {
    // Longest line + CRLF + NUL. The header is 367 characters with CRLF;
    // the longest possible measurement row is checked in test_csv.
    static constexpr size_t kCapacity = 512;

    char text[kCapacity] = {}; // NUL-terminated, ends with "\r\n"
    uint16_t length = 0;       // characters in text, without the NUL
};
