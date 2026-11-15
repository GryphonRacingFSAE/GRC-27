#include "LineReader.h"

LineEvent LineReader::push(char c) {
    if (c == '\r' || c == '\n') {
        const bool too_long = too_long_;
        const bool bad = bad_;
        const bool empty = len_ == 0;
        buf_[len_] = '\0';
        len_ = 0;
        too_long_ = false;
        bad_ = false;
        if (too_long) {
            return LineEvent::TooLong;
        }
        if (bad) {
            return LineEvent::BadByte;
        }
        return empty ? LineEvent::None : LineEvent::Line;
    }
    const unsigned char b = static_cast<unsigned char>(c);
    if ((b < 0x20 && b != '\t') || b >= 0x7f) {
        bad_ = true;
    }
    if (len_ == kMaxLine) { // keeps saying so for the rest of the line
        too_long_ = true;
        return LineEvent::None;
    }
    buf_[len_++] = c;
    return LineEvent::None;
}
