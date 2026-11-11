#include <LoadCellMonitorTask.h>

#include <API.h>
#include <DynoConfig.h>
#include <LoadCellAcquirer.h>

static const char* statusName(LoadCellStatus status) {
    switch (status) {
    case LoadCellStatus::NoSampleYet:
        return "NoSampleYet";
    case LoadCellStatus::Valid:
        return "Valid";
    case LoadCellStatus::Fault:
        return "Fault";
    }
    return "?";
}

void LoadCellMonitorTask(void* pvParameters) {
    const LoadCellMonitorTaskParams* params = static_cast<const LoadCellMonitorTaskParams*>(pvParameters);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_diagnostics_ms = millis();

    for (;;) {
        LoadCellSample sample;
        LoadCellCounters counters;
        xQueuePeek(params->sampleMailbox, &sample, 0);     // both mailboxes are filled at
        xQueuePeek(params->countersMailbox, &counters, 0); // creation, so never empty

        Serial.printf("# loadcell raw=%ld t=%lu status=%s timeouts=%lu faults=%lu recoveries=%lu samples=%lu\r\n",
                      static_cast<long>(sample.raw), static_cast<unsigned long>(sample.timestamp_ms),
                      statusName(sample.status), static_cast<unsigned long>(counters.timeouts),
                      static_cast<unsigned long>(counters.faults_entered),
                      static_cast<unsigned long>(counters.recoveries),
                      static_cast<unsigned long>(counters.samples));

        // Boot messages are often missed (USB serial connects late): repeat them.
        const uint32_t now_ms = millis();
        if (now_ms - last_diagnostics_ms >= DIAGNOSTIC_REPEAT_INTERVAL_MS) {
            printDiagnosticWarnings();
            last_diagnostics_ms = now_ms;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LOADCELL_MONITOR_INTERVAL_MS));
    }
}
