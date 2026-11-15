#include <OutputTask.h>

// Writes one whole line. Only hands Serial what fits in its transmit buffer
// right now, waiting otherwise: Serial.write() gives up after a timeout when
// the PC stops reading and would send half a line. Waiting here is fine:
// nothing else runs in this task, and producers never wait for it.
static void writeWholeLine(const OutputLine& line) {
    const uint8_t* text = reinterpret_cast<const uint8_t*>(line.text);
    size_t sent = 0;
    while (sent < line.length) {
        const int space = Serial.availableForWrite();
        if (space <= 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        size_t chunk = line.length - sent;
        if (chunk > static_cast<size_t>(space)) {
            chunk = static_cast<size_t>(space);
        }
        sent += Serial.write(text + sent, chunk);
    }
}

void OutputTask(void* pvParameters) {
    const OutputTaskParams* params = static_cast<const OutputTaskParams*>(pvParameters);
    OutputLine line; // static size, one at a time: no heap
    for (;;) {
        // Sleeping until a line arrives is this task's whole job.
        if (xQueueReceive(params->outputQueue, &line, portMAX_DELAY) == pdTRUE) {
            writeWholeLine(line);
        }
    }
}
