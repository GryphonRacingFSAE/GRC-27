#include <API.h>

#include <Arduino.h>
#include <DynoConfig.h>
#include <pindefs.h>

// Diagnostics use the "# " prefix so CSV parsers skip them.
void printDiagnosticWarnings() {
#if !DYNO_PINS_CONFIRMED
    Serial.print("# WARNING: pin assignments are unconfirmed placeholders (see include/Defines/pindefs.h)\r\n");
#endif
#if DYNO_FAKE_INPUTS
    Serial.print("# FAKE INPUTS: load cell and ECU CAN are simulated (DYNO_FAKE_INPUTS=1), not reading the HX711 or the CAN bus\r\n");
#endif
}

void haltWithError(const char* message) {
    for (;;) {
        Serial.printf("# ERROR: %s\r\n", message);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
