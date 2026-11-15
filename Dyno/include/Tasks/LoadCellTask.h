// Reads the HX711 (or the fake, with DYNO_FAKE_INPUTS) and publishes the
// latest LoadCellSample into a 1-slot mailbox. Also owns the calibration
// (#35): it loads the saved one at boot, carries out the operator's
// tare/cal commands, and publishes the latest ForceMeasurement.
#pragma once

#include <Arduino.h>
#include <LoadCellAcquirer.h>

struct LoadCellTaskParams {
    QueueHandle_t sampleMailbox;   // holds 1 LoadCellSample; read it with xQueuePeek
    QueueHandle_t countersMailbox; // holds 1 LoadCellCounters (for the monitor printout)
    QueueHandle_t forceMailbox;    // holds 1 ForceMeasurement; read it with xQueuePeek
    QueueHandle_t commandQueue;    // LoadCellCommand from CommandTask (taken, never waited on)
};

void LoadCellTask(void* pvParameters);
