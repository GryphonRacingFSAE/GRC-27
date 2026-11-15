#include "TareButton.h"

#include <stdio.h>

TareButton::TareButton(ButtonInput& input, CommandSink& sink, CalibrationReplies& replies,
                       const TareButtonConfig& config)
    : input_(input), sink_(sink), replies_(replies), active_low_(config.active_low),
      debouncer_(config.debounce_ms) {}

void TareButton::step(uint32_t now_ms) {
    if (!debouncer_.step(pressedFromLevel(input_.levelHigh(), active_low_), now_ms)) {
        return;
    }
    LoadCellCommand tare;
    tare.kind = CommandKind::Tare;
    if (!sink_.send(tare)) {
        replies_.reply("button: tare dropped (too many commands at once); press again");
        return;
    }
    char line[64];
    snprintf(line, sizeof line, "button: tare pressed (presses=%lu bounces=%lu)", (unsigned long)presses(),
             (unsigned long)bounces());
    replies_.reply(line);
}
