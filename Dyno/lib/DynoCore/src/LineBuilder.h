// Builds an OutputLine piece by piece with printf-style appends. If anything
// doesn't fit, the builder remembers it and finish() returns false and leaves
// the line EMPTY: a line is either complete or not printed, never truncated.
#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "OutputLine.h"

class LineBuilder {
  public:
    explicit LineBuilder(OutputLine& line, size_t capacity = OutputLine::kCapacity)
        : line_(line), capacity_(capacity < OutputLine::kCapacity ? capacity : OutputLine::kCapacity) {
        line_.text[0] = '\0';
        line_.length = 0;
    }

    void append(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (overflowed_) return;
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(line_.text + len_, capacity_ - len_, fmt, args);
        va_end(args);
        // n >= space left: it was cut short (room is needed for the NUL too).
        if (n < 0 || static_cast<size_t>(n) >= capacity_ - len_) {
            overflowed_ = true;
            return;
        }
        len_ += static_cast<size_t>(n);
    }

    bool overflowed() const { return overflowed_; }

    // true = the line is complete. false = it overflowed; the line is emptied.
    bool finish() {
        if (overflowed_) {
            line_.text[0] = '\0';
            line_.length = 0;
            return false;
        }
        line_.length = static_cast<uint16_t>(len_);
        return true;
    }

  private:
    OutputLine& line_;
    size_t capacity_;
    size_t len_ = 0;
    bool overflowed_ = false;
};
