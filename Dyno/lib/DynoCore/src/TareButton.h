// The operator's tare button (#36). Polled by CommandTask: each step reads
// the pin, debounces it, and on a press sends the same Tare command that
// typing "tare" sends. The calibrator then does the tare (or refuses it)
// exactly as for the typed command, so the button adds no tare logic.
#pragma once

#include <stdint.h>

#include "ButtonDebouncer.h"
#include "ButtonInput.h"
#include "CommandParser.h"
#include "LoadCellCalibrator.h"

// Where commands go: on the ESP32 the load-cell command queue, sent without
// waiting. False = not accepted (the queue is full).
class CommandSink {
  public:
    virtual ~CommandSink() = default;
    virtual bool send(const LoadCellCommand& command) = 0;
};

struct TareButtonConfig {
    bool active_low;      // true: pull-up, button to ground, LOW = pressed
    uint32_t debounce_ms; // a press counts once it has lasted this long
};

class TareButton {
  public:
    TareButton(ButtonInput& input, CommandSink& sink, CalibrationReplies& replies, const TareButtonConfig& config);

    // Read the pin once; on an accepted press, send Tare and say so.
    void step(uint32_t now_ms);

    uint32_t presses() const { return debouncer_.presses(); }
    uint32_t bounces() const { return debouncer_.bounces(); }

  private:
    ButtonInput& input_;
    CommandSink& sink_;
    CalibrationReplies& replies_;
    bool active_low_;
    ButtonDebouncer debouncer_;
};
