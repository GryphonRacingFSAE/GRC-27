#include <TaskFactory.h>

#include <API.h>
#include <Arduino.h>
#include <CanReceiver.h>
#include <CanTask.h>
#include <CommandParser.h>
#include <CommandTask.h>
#include <DynoConfig.h>
#include <ForceMeasurement.h>
#include <LoadCellAcquirer.h>
#include <CsvTask.h>
#include <LoadCellTask.h>
#include <OutputLine.h>
#include <OutputTask.h>

// Mailboxes: 1-slot queues. The writer replaces the contents with
// xQueueOverwrite; readers look with xQueuePeek (doesn't remove it).
static QueueHandle_t loadCellSampleMailbox = NULL;
static QueueHandle_t loadCellCountersMailbox = NULL;
static QueueHandle_t forceMailbox = NULL;
static QueueHandle_t ecuMailbox = NULL;
static QueueHandle_t canStatusMailbox = NULL;

static TaskHandle_t loadCellTaskHandle = NULL;
static LoadCellTaskParams loadCellParams;
static TaskHandle_t canTaskHandle = NULL;
static CanTaskParams canParams;

// Typed commands (LoadCellCommand) from CommandTask to LoadCellTask (#35).
static QueueHandle_t commandQueue = NULL;
static TaskHandle_t commandTaskHandle = NULL;
static CommandTaskParams commandParams;

// The bounded queue of complete lines (CSV rows, "# " lines) for OutputTask.
static QueueHandle_t outputQueue = NULL;

static TaskHandle_t csvTaskHandle = NULL;
static CsvTaskParams csvParams;
static TaskHandle_t outputTaskHandle = NULL;
static OutputTaskParams outputParams;

void createTasks() {
    // 1. Mailboxes, pre-filled so a reader never finds them empty.
    loadCellSampleMailbox = xQueueCreate(1, sizeof(LoadCellSample));
    loadCellCountersMailbox = xQueueCreate(1, sizeof(LoadCellCounters));
    forceMailbox = xQueueCreate(1, sizeof(ForceMeasurement));
    commandQueue = xQueueCreate(COMMAND_QUEUE_LENGTH, sizeof(LoadCellCommand));
    if (loadCellSampleMailbox == NULL || loadCellCountersMailbox == NULL || forceMailbox == NULL ||
        commandQueue == NULL) {
        haltWithError("could not create load-cell mailboxes (out of memory)");
    }
    const LoadCellSample noSampleYet;
    const LoadCellCounters zeroCounters;
    const ForceMeasurement noForceYet; // available = false, calibration_ready = false
    xQueueOverwrite(loadCellSampleMailbox, &noSampleYet);
    xQueueOverwrite(loadCellCountersMailbox, &zeroCounters);
    xQueueOverwrite(forceMailbox, &noForceYet);

    ecuMailbox = xQueueCreate(1, sizeof(EcuState));
    canStatusMailbox = xQueueCreate(1, sizeof(CanStatus));
    if (ecuMailbox == NULL || canStatusMailbox == NULL) {
        haltWithError("could not create CAN mailboxes (out of memory)");
    }
    const EcuState nothingReceived; // every message: received = false
    const CanStatus notStarted;     // link = Starting, zero counters
    xQueueOverwrite(ecuMailbox, &nothingReceived);
    xQueueOverwrite(canStatusMailbox, &notStarted);

    outputQueue = xQueueCreate(CSV_OUTPUT_QUEUE_LINES, sizeof(OutputLine));
    if (outputQueue == NULL) {
        haltWithError("could not create the output queue (out of memory)");
    }
    setDiagnosticQueue(outputQueue);

    // 2. Params
    loadCellParams.sampleMailbox = loadCellSampleMailbox;
    loadCellParams.countersMailbox = loadCellCountersMailbox;
    loadCellParams.forceMailbox = forceMailbox;
    loadCellParams.commandQueue = commandQueue;
    commandParams.commandQueue = commandQueue;
    canParams.ecuMailbox = ecuMailbox;
    canParams.statusMailbox = canStatusMailbox;
    csvParams.loadCellSampleMailbox = loadCellSampleMailbox;
    csvParams.loadCellCountersMailbox = loadCellCountersMailbox;
    csvParams.forceMailbox = forceMailbox;
    csvParams.ecuMailbox = ecuMailbox;
    csvParams.canStatusMailbox = canStatusMailbox;
    csvParams.outputQueue = outputQueue;
    outputParams.outputQueue = outputQueue;

    // 3. Tasks
    // 6 KB (was 4 KB): it now also formats calibration replies and writes
    // NVS (flash) when a calibration is saved.
    if (xTaskCreate(LoadCellTask, "LoadCellTask", 6144, &loadCellParams, 2, &loadCellTaskHandle) != pdPASS) {
        haltWithError("could not create LoadCellTask (out of memory)");
    }
    if (xTaskCreate(CanTask, "CanTask", 4096, &canParams, 2, &canTaskHandle) != pdPASS) {
        haltWithError("could not create CanTask (out of memory)");
    }

    // CsvTask: same priority as acquisition (short, fixed-rate work that must
    // not be starved). 6 KB stack: two 512-byte lines + printf of doubles.
    if (xTaskCreate(CsvTask, "CsvTask", 6144, &csvParams, 2, &csvTaskHandle) != pdPASS) {
        haltWithError("could not create CsvTask (out of memory)");
    }
    // CommandTask: lowest priority, like OutputTask. Typing is slow, and a
    // command is only queued here; LoadCellTask does the work.
    if (xTaskCreate(CommandTask, "CommandTask", 4096, &commandParams, 1, &commandTaskHandle) != pdPASS) {
        haltWithError("could not create CommandTask (out of memory)");
    }
    // OutputTask LAST and lowest priority: the only serial writer from here
    // on (haltWithError above still printed directly, safely, before it
    // existed), and a slow PC can only ever hold up this task.
    if (xTaskCreate(OutputTask, "OutputTask", 3072, &outputParams, 1, &outputTaskHandle) != pdPASS) {
        haltWithError("could not create OutputTask (out of memory)");
    }
}
