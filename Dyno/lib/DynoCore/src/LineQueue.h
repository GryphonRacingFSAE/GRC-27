// The bounded queue between the row/diagnostic producers and the single
// serial writer (#38). tryPush never waits: when the queue is full the NEW
// line is refused and the caller counts it.
//
// Firmware: a FreeRTOS queue (src/Tasks/OutputTask). Host tests:
// BoundedLineQueue below, which has the same "full = refuse" behaviour.
#pragma once

#include <stddef.h>

#include "OutputLine.h"

class LineQueue {
  public:
    virtual ~LineQueue() = default;
    // false = full: the line was NOT queued. Never blocks.
    virtual bool tryPush(const OutputLine& line) = 0;
};

// Fixed-capacity FIFO, no heap. Not thread-safe (host tests only).
template <size_t Capacity>
class BoundedLineQueue : public LineQueue {
  public:
    bool tryPush(const OutputLine& line) override {
        if (count_ == Capacity) return false;
        slots_[(head_ + count_) % Capacity] = line;
        ++count_;
        return true;
    }

    bool tryPop(OutputLine& out) {
        if (count_ == 0) return false;
        out = slots_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return true;
    }

    size_t size() const { return count_; }
    static constexpr size_t capacity() { return Capacity; }

  private:
    OutputLine slots_[Capacity];
    size_t head_ = 0;
    size_t count_ = 0;
};
