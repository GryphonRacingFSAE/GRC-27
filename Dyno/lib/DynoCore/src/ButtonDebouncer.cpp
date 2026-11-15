#include "ButtonDebouncer.h"

bool ButtonDebouncer::step(bool pressed, uint32_t now_ms) {
    if (!started_) {
        // The first poll is the starting state, not a change: a button held
        // through boot must be released and pressed again to count.
        started_ = true;
        stable_ = pressed;
        last_ = pressed;
        return false;
    }
    if (pressed != last_) {
        if (last_ != stable_) {
            ++bounces_; // a change that didn't last: undone before it was accepted
        }
        last_ = pressed;
        changed_ms_ = now_ms;
    }
    if (last_ == stable_ || now_ms - changed_ms_ < debounce_ms_) {
        return false;
    }
    stable_ = last_;
    if (!stable_) {
        return false;
    }
    ++presses_;
    return true;
}
