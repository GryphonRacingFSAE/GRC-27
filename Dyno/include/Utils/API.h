// Use if needed to include any API functions that are used in multiple files.
// Custom API functions that are used in multiple files can be included here.
#pragma once

// Prints the "# WARNING" / "# FAKE INPUTS" lines that apply to this build.
// Called at boot and repeated by the monitor task.
void printDiagnosticWarnings();

// Prints "# ERROR: <message>" once a second forever. For startup failures
// (out of memory) where carrying on would mean running half a dyno.
[[noreturn]] void haltWithError(const char* message);
