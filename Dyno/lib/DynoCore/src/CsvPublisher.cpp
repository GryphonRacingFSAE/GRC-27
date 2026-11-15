#include "CsvPublisher.h"

#include <stdio.h>

#include "CsvSchema.h"

static const char kDiagnosticTooLong[] = "# ERROR: diagnostic line too long, not printed\r\n";

void formatDiagnosticV(OutputLine& line, const char* fmt, va_list args) {
    const size_t cap = OutputLine::kCapacity;
    const size_t crlf = 2;
    int n = snprintf(line.text, cap, "# ");
    size_t len = static_cast<size_t>(n);
    // Leave room for CRLF + NUL after the text.
    n = vsnprintf(line.text + len, cap - len - crlf, fmt, args);
    if (n < 0 || static_cast<size_t>(n) >= cap - len - crlf) {
        snprintf(line.text, cap, "%s", kDiagnosticTooLong);
        line.length = static_cast<uint16_t>(sizeof(kDiagnosticTooLong) - 1);
        return;
    }
    len += static_cast<size_t>(n);
    line.text[len++] = '\r';
    line.text[len++] = '\n';
    line.text[len] = '\0';
    line.length = static_cast<uint16_t>(len);
}

void formatDiagnostic(OutputLine& line, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    formatDiagnosticV(line, fmt, args);
    va_end(args);
}

CsvPublisher::CsvPublisher(LineQueue& queue, const SnapshotConfig& config, size_t row_capacity)
    : queue_(queue), config_(config), row_capacity_(row_capacity) {}

bool CsvPublisher::start() {
    if (started_) return false;
    OutputLine header;
    if (!formatCsvHeader(header) || !queue_.tryPush(header)) return false;
    started_ = true;
    return true;
}

DynoSnapshot CsvPublisher::publishRow(const MeasurementInputs& in, uint32_t now_ms) {
    const DynoSnapshot snap = takeSnapshot(in, config_, now_ms, counters_.dropped_rows);
    OutputLine row;
    if (!formatCsvRow(snap, row, row_capacity_)) {
        ++counters_.row_overflows;
        OutputLine error;
        formatDiagnostic(error, "ERROR: CSV row too long, not printed (%lu so far)",
                         static_cast<unsigned long>(counters_.row_overflows));
        queueDiagnostic(error);
        return snap;
    }
    if (queue_.tryPush(row)) {
        ++counters_.rows_queued;
    } else {
        ++counters_.dropped_rows;
    }
    return snap;
}

bool CsvPublisher::diagnostic(const char* fmt, ...) {
    OutputLine line;
    va_list args;
    va_start(args, fmt);
    formatDiagnosticV(line, fmt, args);
    va_end(args);
    return queueDiagnostic(line);
}

bool CsvPublisher::queueDiagnostic(const OutputLine& line) {
    if (queue_.tryPush(line)) return true;
    ++counters_.dropped_diagnostics;
    return false;
}
