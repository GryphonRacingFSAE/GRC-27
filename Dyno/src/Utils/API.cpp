#include <API.h>

#include <Arduino.h>
#include <CsvPublisher.h>
#include <DynoConfig.h>
#include <atomic>
#include <pindefs.h>
#include <stdarg.h>

static QueueHandle_t diagnosticQueue = NULL;
static std::atomic<uint32_t> diagnosticDrops{0};

void setDiagnosticQueue(QueueHandle_t outputQueue) { diagnosticQueue = outputQueue; }

bool printDiagnostic(const char* fmt, ...) {
    OutputLine line;
    va_list args;
    va_start(args, fmt);
    formatDiagnosticV(line, fmt, args); // too long -> a fixed "# ERROR" line, never cut
    va_end(args);
    if (diagnosticQueue != NULL && xQueueSend(diagnosticQueue, &line, 0) == pdTRUE) {
        return true;
    }
    diagnosticDrops.fetch_add(1);
    return false;
}

uint32_t diagnosticLinesDropped() { return diagnosticDrops.load(); }

// Diagnostics use the "# " prefix so CSV parsers skip them.
void printDiagnosticWarnings() {
#if !DYNO_PINS_CONFIRMED
    printDiagnostic("WARNING: pin assignments are unconfirmed placeholders (see include/Defines/pindefs.h)");
#endif
    if (!GEOMETRY_CONFIRMED) {
        printDiagnostic("WARNING: dyno geometry unconfirmed (DynoConfig.h): torque and power are disabled");
    }
    if (!SAME_SHAFT_CONFIRMED) {
        printDiagnostic("WARNING: ECU RPM not confirmed to be the load-cell shaft (DynoConfig.h): power is disabled");
    }
    printDiagnostic("WARNING: no calibrated force yet (#35 not merged): force, torque and power columns are blank");
#if DYNO_FAKE_INPUTS
    printDiagnostic("FAKE INPUTS: load cell and ECU CAN are simulated (DYNO_FAKE_INPUTS=1), not reading the HX711 or "
                    "the CAN bus");
#endif
}

void haltWithError(const char* message) {
    for (;;) {
        Serial.printf("# ERROR: %s\r\n", message);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
