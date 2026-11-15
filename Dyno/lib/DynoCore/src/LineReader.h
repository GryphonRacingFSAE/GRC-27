// Builds typed serial input into whole lines, one byte at a time (#35). It
// never waits and never allocates: the command task hands it whatever bytes
// have arrived, and it says when a full line is ready. Lines may end with LF,
// CR or CRLF (serial monitors differ); empty lines are ignored.
//
// A line longer than kMaxLine is thrown away up to its line end and reported
// once, as TooLong. A line holding a control character or a non-ASCII byte
// (line noise, wrong baud rate) is reported as BadByte instead of being
// parsed. Tabs are allowed.
#pragma once

#include <stddef.h>

enum class LineEvent { None, Line, TooLong, BadByte };

class LineReader {
  public:
    static constexpr size_t kMaxLine = 64; // characters, not counting the line end

    // Feed one byte. Returns Line when a line just ended (read it with line()),
    // TooLong or BadByte when a rejected line just ended, otherwise None.
    LineEvent push(char c);

    // The last line, valid until the next push().
    const char* line() const { return buf_; }

  private:
    char buf_[kMaxLine + 1] = {};
    size_t len_ = 0;
    bool too_long_ = false; // this line overflowed: skipping to its end
    bool bad_ = false;      // this line held a bad byte
};
