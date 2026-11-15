#include <CommandTask.h>

#include <API.h>
#include <CommandParser.h>
#include <DynoCalibrationSetup.h>
#include <DynoConfig.h>
#include <GpioButton.h>
#include <LineReader.h>
#include <TareButton.h>
#include <pindefs.h>

// The tare button's press goes into the same queue as a typed command,
// without waiting (a full queue is reported by TareButton, not retried).
class QueueCommandSink : public CommandSink {
  public:
    explicit QueueCommandSink(QueueHandle_t queue) : queue_(queue) {}
    bool send(const LoadCellCommand& command) override { return xQueueSend(queue_, &command, 0) == pdTRUE; }

  private:
    QueueHandle_t queue_;
};

// The button's "# button: ..." lines, through the one serial writer.
class DiagnosticReplies : public CalibrationReplies {
  public:
    void reply(const char* text) override { printDiagnostic("%s", text); }
};

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

    // The operator's tare button (#36), polled once per pass below.
    static GpioButton pin(PIN_TARE_BUTTON, TARE_BUTTON_ACTIVE_LOW);
    static QueueCommandSink sink(params->commandQueue);
    static DiagnosticReplies replies;
    static TareButton tareButton(pin, sink, replies, dynoTareButtonConfig());
    pin.begin();

    for (;;) {
        tareButton.step(millis());

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
