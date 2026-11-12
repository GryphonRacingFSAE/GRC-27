// TEMPORARY (removed by #38): prints the load-cell and ECU mailboxes at 2 Hz
// as "# loadcell ...", "# ecu ...", "# can ..." and "# calc ..." (torque/power,
// #37) lines so the hardware can be checked over USB serial before the CSV
// output exists. #38 moves the torque/power call into its output task.
#pragma once

#include <Arduino.h>

struct MonitorTaskParams {
    QueueHandle_t loadCellSampleMailbox;
    QueueHandle_t loadCellCountersMailbox;
    QueueHandle_t ecuMailbox;
    QueueHandle_t canStatusMailbox;
};

void MonitorTask(void* pvParameters);
