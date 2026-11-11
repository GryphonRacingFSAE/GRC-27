// Reads the HX711 (or the fake, with DYNO_FAKE_INPUTS) and publishes the
// latest LoadCellSample into a 1-slot mailbox.
#pragma once

#include <Arduino.h>
#include <LoadCellAcquirer.h>

struct LoadCellTaskParams {
    QueueHandle_t sampleMailbox;   // holds 1 LoadCellSample; read it with xQueuePeek
    QueueHandle_t countersMailbox; // holds 1 LoadCellCounters (for the monitor printout)
};

void LoadCellTask(void* pvParameters);
