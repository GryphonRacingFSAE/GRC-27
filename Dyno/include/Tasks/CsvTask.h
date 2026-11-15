// Combines the latest measurements into one CSV row every CSV_ROW_INTERVAL_MS
// (#38) and queues it for OutputTask. Also queues the low-rate status lines
// ("# loadcell", "# can", "# calc", "# output") every STATUS_LINE_INTERVAL_MS
// and repeats the startup warnings. Never waits for inputs or for the serial
// port, so rows keep coming during CAN silence or a load-cell fault.
// See docs/csv-output.md.
#pragma once

#include <Arduino.h>

struct CsvTaskParams {
    QueueHandle_t loadCellSampleMailbox;   // LoadCellSample (peek)
    QueueHandle_t loadCellCountersMailbox; // LoadCellCounters (peek)
    QueueHandle_t forceMailbox;            // ForceMeasurement (peek), from LoadCellTask (#35)
    QueueHandle_t ecuMailbox;              // EcuState (peek)
    QueueHandle_t canStatusMailbox;        // CanStatus (peek)
    QueueHandle_t outputQueue;             // OutputLine, to OutputTask
};

void CsvTask(void* pvParameters);
