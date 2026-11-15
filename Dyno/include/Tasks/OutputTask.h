// The serial writer (#38): the ONLY task that writes to Serial once tasks are
// running. Everyone else puts complete lines (CSV rows, "# " lines) into the
// bounded output queue without waiting; this task prints them in order.
// If the PC reads slowly, only this task waits: the queue fills and the
// producers drop and count new rows. See docs/csv-output.md.
#pragma once

#include <Arduino.h>
#include <LineQueue.h>
#include <OutputLine.h>

struct OutputTaskParams {
    QueueHandle_t outputQueue; // holds OutputLine items
};

void OutputTask(void* pvParameters);

// LineQueue on the FreeRTOS output queue. tryPush never waits (timeout 0).
class FreeRtosLineQueue : public LineQueue {
  public:
    explicit FreeRtosLineQueue(QueueHandle_t queue) : queue_(queue) {}
    bool tryPush(const OutputLine& line) override { return xQueueSend(queue_, &line, 0) == pdTRUE; }
    uint32_t spacesLeft() const { return static_cast<uint32_t>(uxQueueSpacesAvailable(queue_)); }

  private:
    QueueHandle_t queue_;
};
