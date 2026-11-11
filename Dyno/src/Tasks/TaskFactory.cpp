#include <TaskFactory.h>

#include <API.h>
#include <Arduino.h>
#include <DynoConfig.h>
#include <LoadCellAcquirer.h>
#include <LoadCellMonitorTask.h>
#include <LoadCellTask.h>

// Mailboxes: 1-slot queues. The writer replaces the contents with
// xQueueOverwrite; readers look with xQueuePeek (doesn't remove it).
static QueueHandle_t loadCellSampleMailbox = NULL;
static QueueHandle_t loadCellCountersMailbox = NULL;

static TaskHandle_t loadCellTaskHandle = NULL;
static LoadCellTaskParams loadCellParams;

#if DYNO_LOADCELL_MONITOR
static TaskHandle_t loadCellMonitorTaskHandle = NULL;
static LoadCellMonitorTaskParams loadCellMonitorParams;
#endif

void createTasks() {
    // 1. Mailboxes, pre-filled so a reader never finds them empty.
    loadCellSampleMailbox = xQueueCreate(1, sizeof(LoadCellSample));
    loadCellCountersMailbox = xQueueCreate(1, sizeof(LoadCellCounters));
    if (loadCellSampleMailbox == NULL || loadCellCountersMailbox == NULL) {
        haltWithError("could not create load-cell mailboxes (out of memory)");
    }
    const LoadCellSample noSampleYet;
    const LoadCellCounters zeroCounters;
    xQueueOverwrite(loadCellSampleMailbox, &noSampleYet);
    xQueueOverwrite(loadCellCountersMailbox, &zeroCounters);

    // 2. Params
    loadCellParams.sampleMailbox = loadCellSampleMailbox;
    loadCellParams.countersMailbox = loadCellCountersMailbox;

    // 3. Tasks
    if (xTaskCreate(LoadCellTask, "LoadCellTask", 4096, &loadCellParams, 2, &loadCellTaskHandle) != pdPASS) {
        haltWithError("could not create LoadCellTask (out of memory)");
    }

#if DYNO_LOADCELL_MONITOR
    loadCellMonitorParams.sampleMailbox = loadCellSampleMailbox;
    loadCellMonitorParams.countersMailbox = loadCellCountersMailbox;
    if (xTaskCreate(LoadCellMonitorTask, "LoadCellMonitor", 4096, &loadCellMonitorParams, 1,
                    &loadCellMonitorTaskHandle) != pdPASS) {
        haltWithError("could not create LoadCellMonitorTask (out of memory)");
    }
#endif
}
