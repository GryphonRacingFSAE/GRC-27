#include <TaskFactory.h>

#include <API.h>
#include <Arduino.h>
#include <CanReceiver.h>
#include <CanTask.h>
#include <DynoConfig.h>
#include <LoadCellAcquirer.h>
#include <LoadCellTask.h>
#include <MonitorTask.h>

// Mailboxes: 1-slot queues. The writer replaces the contents with
// xQueueOverwrite; readers look with xQueuePeek (doesn't remove it).
static QueueHandle_t loadCellSampleMailbox = NULL;
static QueueHandle_t loadCellCountersMailbox = NULL;
static QueueHandle_t ecuMailbox = NULL;
static QueueHandle_t canStatusMailbox = NULL;

static TaskHandle_t loadCellTaskHandle = NULL;
static LoadCellTaskParams loadCellParams;
static TaskHandle_t canTaskHandle = NULL;
static CanTaskParams canParams;

#if DYNO_SERIAL_MONITOR
static TaskHandle_t monitorTaskHandle = NULL;
static MonitorTaskParams monitorParams;
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

    ecuMailbox = xQueueCreate(1, sizeof(EcuState));
    canStatusMailbox = xQueueCreate(1, sizeof(CanStatus));
    if (ecuMailbox == NULL || canStatusMailbox == NULL) {
        haltWithError("could not create CAN mailboxes (out of memory)");
    }
    const EcuState nothingReceived; // every message: received = false
    const CanStatus notStarted;     // link = Starting, zero counters
    xQueueOverwrite(ecuMailbox, &nothingReceived);
    xQueueOverwrite(canStatusMailbox, &notStarted);

    // 2. Params
    loadCellParams.sampleMailbox = loadCellSampleMailbox;
    loadCellParams.countersMailbox = loadCellCountersMailbox;
    canParams.ecuMailbox = ecuMailbox;
    canParams.statusMailbox = canStatusMailbox;

    // 3. Tasks
    if (xTaskCreate(LoadCellTask, "LoadCellTask", 4096, &loadCellParams, 2, &loadCellTaskHandle) != pdPASS) {
        haltWithError("could not create LoadCellTask (out of memory)");
    }
    if (xTaskCreate(CanTask, "CanTask", 4096, &canParams, 2, &canTaskHandle) != pdPASS) {
        haltWithError("could not create CanTask (out of memory)");
    }

#if DYNO_SERIAL_MONITOR
    monitorParams.loadCellSampleMailbox = loadCellSampleMailbox;
    monitorParams.loadCellCountersMailbox = loadCellCountersMailbox;
    monitorParams.ecuMailbox = ecuMailbox;
    monitorParams.canStatusMailbox = canStatusMailbox;
    if (xTaskCreate(MonitorTask, "Monitor", 4096, &monitorParams, 1, &monitorTaskHandle) != pdPASS) {
        haltWithError("could not create MonitorTask (out of memory)");
    }
#endif
}
