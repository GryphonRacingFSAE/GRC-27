// TEMPORARY (removed by #38): prints the load-cell mailbox at 2 Hz as
// "# loadcell ..." lines so the hardware can be checked over USB serial.
#pragma once

#include <Arduino.h>

struct LoadCellMonitorTaskParams {
    QueueHandle_t sampleMailbox;
    QueueHandle_t countersMailbox;
};

void LoadCellMonitorTask(void* pvParameters);
