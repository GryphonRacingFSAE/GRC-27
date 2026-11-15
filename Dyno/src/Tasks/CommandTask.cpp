#include <CommandTask.h>

#include <API.h>
#include <CommandParser.h>
#include <DynoConfig.h>
#include <LineReader.h>

static void handleLine(const char* line, QueueHandle_t commandQueue) {
    const ParseResult parsed = parseCommand(line);
    if (parsed.error != ParseError::None) {
        printDiagnostic("cmd: \"%s\" not understood (%s); commands: tare, cal unloaded, cal loaded <value>N|kg, "
                        "cal cancel, cal status",
                        line, parseErrorName(parsed.error));
        return;
    }
    // Never wait: LoadCellTask takes one command per pass (every few ms), so
    // a full queue means someone is pasting faster than any operator types.
    if (xQueueSend(commandQueue, &parsed.command, 0) != pdTRUE) {
        printDiagnostic("cmd: \"%s\" dropped (too many commands at once); type it again", line);
    }
}

void CommandTask(void* pvParameters) {
    const CommandTaskParams* params = static_cast<const CommandTaskParams*>(pvParameters);
    LineReader reader;

    for (;;) {
        // A bounded number of bytes per pass, so a flood of input (or line
        // noise) can't keep this task busy.
        for (uint32_t i = 0; i < COMMAND_MAX_BYTES_PER_POLL && Serial.available() > 0; ++i) {
            const int byte = Serial.read();
            if (byte < 0) {
                break;
            }
            switch (reader.push(static_cast<char>(byte))) {
            case LineEvent::None:
                break;
            case LineEvent::Line:
                handleLine(reader.line(), params->commandQueue);
                break;
            case LineEvent::TooLong:
                printDiagnostic("cmd: line ignored (longer than %u characters)",
                                static_cast<unsigned>(LineReader::kMaxLine));
                break;
            case LineEvent::BadByte:
                printDiagnostic("cmd: line ignored (unexpected control or non-ASCII character; check the baud rate)");
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(COMMAND_POLL_INTERVAL_MS));
    }
}
