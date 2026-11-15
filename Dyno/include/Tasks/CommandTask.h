// Reads operator commands typed into the serial monitor (#35): "tare",
// "cal unloaded", "cal loaded 100N", "cal cancel", "cal status". Each
// complete line is parsed; a good command is queued for LoadCellTask (which
// owns the calibration), a bad one gets a "# cmd: ..." reply. It never
// writes to Serial itself: replies go through printDiagnostic(), so
// OutputTask stays the only serial writer. See docs/calibration.md.
#pragma once

#include <Arduino.h>

struct CommandTaskParams {
    QueueHandle_t commandQueue; // LoadCellCommand, to LoadCellTask
};

void CommandTask(void* pvParameters);
