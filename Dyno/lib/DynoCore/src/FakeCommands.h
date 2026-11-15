// Test doubles for where load-cell commands go (#36) and what the calibrator
// and the tare button say back (#35/#36). Shared by the host tests and the
// on-device tare-button test.
#pragma once

#include <stddef.h>

#include <deque>
#include <string>
#include <vector>

#include "CommandParser.h"
#include "LoadCellCalibrator.h"
#include "TareButton.h"

// Like the FreeRTOS command queue: holds `capacity` commands and refuses
// more. The test takes commands out of `queued` itself.
class FakeCommandSink : public CommandSink {
  public:
    size_t capacity = 4; // like COMMAND_QUEUE_LENGTH
    std::deque<LoadCellCommand> queued;

    bool send(const LoadCellCommand& command) override {
        if (queued.size() >= capacity) return false;
        queued.push_back(command);
        return true;
    }

    // Commands of this kind still in the queue.
    size_t count(CommandKind kind) const {
        size_t n = 0;
        for (const LoadCellCommand& c : queued) {
            if (c.kind == kind) ++n;
        }
        return n;
    }
};

// Keeps every reply line, in order.
class FakeReplies : public CalibrationReplies {
  public:
    std::vector<std::string> lines;

    void reply(const char* text) override { lines.push_back(text); }

    // Lines that contain `fragment`.
    size_t count(const char* fragment) const {
        size_t n = 0;
        for (const std::string& l : lines) {
            if (l.find(fragment) != std::string::npos) ++n;
        }
        return n;
    }
    bool any(const char* fragment) const { return count(fragment) > 0; }
    // The newest line ("" if none).
    std::string last() const { return lines.empty() ? "" : lines.back(); }
};
