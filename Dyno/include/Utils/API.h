// Use if needed to include any API functions that are used in multiple files.
// Custom API functions that are used in multiple files can be included here.
#pragma once

#include <Arduino.h>

// Where "# " lines go: the output queue that OutputTask prints (#38).
// Set once by createTasks() before any task runs.
void setDiagnosticQueue(QueueHandle_t outputQueue);

// Queues "# <text>\r\n" for OutputTask from ANY task, without waiting.
// Returns false if it was dropped (queue full, or no queue yet); drops are
// counted and shown in the "# output" status line. Use this, never Serial,
// once tasks run: OutputTask is the only serial writer, so lines never mix.
bool printDiagnostic(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t diagnosticLinesDropped();

// Whether a usable calibration is active (#35). LoadCellTask sets it; it
// only decides whether the "not calibrated" warning is repeated.
void setForceCalibrated(bool calibrated);

// Queues the "# WARNING" / "# FAKE INPUTS" lines that apply to this build.
// Sent after the CSV header and repeated by CsvTask.
void printDiagnosticWarnings();

// Prints "# ERROR: <message>" once a second forever, straight to Serial.
// For startup failures (out of memory) where carrying on would mean running
// half a dyno. Only for createTasks(): it runs before OutputTask exists, so
// it's then the only serial writer.
[[noreturn]] void haltWithError(const char* message);
