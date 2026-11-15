// Debounces the operator's tare button (#36). Hardware-free: the caller polls
// the pin and passes in whether it reads "pressed" now.
//
// A mechanical button "bounces": for a few ms after it is pressed or
// released the contacts flicker on and off. The rule: a new level is accepted
// once it has been read steadily for `debounce_ms` (timed from the last
// change seen). Accepting "pressed" gives ONE event; holding it gives no
// more, and nothing happens on release. The level at the first poll is the
// starting state, so a button held through boot (or stuck) only counts after
// it is released and pressed again. Times are millis(): the age is unsigned
// maths, so it keeps working when millis() wraps after ~49.7 days.
#pragma once

#include <stdint.h>

// The pin level as "pressed?". active_low = the button pulls the pin to
// ground against a pull-up, so LOW means pressed.
constexpr bool pressedFromLevel(bool level_high, bool active_low) {
    return active_low ? !level_high : level_high;
}

class ButtonDebouncer {
  public:
    explicit ButtonDebouncer(uint32_t debounce_ms) : debounce_ms_(debounce_ms) {}

    // Feed the latest reading. Returns true exactly once per accepted press.
    bool step(bool pressed, uint32_t now_ms);

    // Accepted presses since boot.
    uint32_t presses() const { return presses_; }
    // Changes undone before they lasted debounce_ms (contact chatter or
    // noise). Shown on hardware so debounce_ms can be checked.
    uint32_t bounces() const { return bounces_; }

  private:
    uint32_t debounce_ms_;
    bool started_ = false;    // seen the first poll yet
    bool stable_ = false;     // the accepted level
    bool last_ = false;       // the level at the last poll
    uint32_t changed_ms_ = 0; // when the level last changed
    uint32_t presses_ = 0;
    uint32_t bounces_ = 0;
};
