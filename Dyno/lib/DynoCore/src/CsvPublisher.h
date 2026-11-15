// Producer side of the serial output (#38): turns measurement snapshots and
// diagnostics into complete lines and offers them to the bounded output
// queue. It never waits: a full queue means the new line is discarded and
// counted, so acquisition and aggregation keep running whatever the serial
// link does. See docs/csv-output.md.
//
// The firmware's aggregation task owns one CsvPublisher and calls
// publishRow() every CSV_ROW_INTERVAL_MS. It is not thread-safe; other tasks
// send their "# " lines with formatDiagnostic() + the queue directly.
#pragma once

#include <stdarg.h>
#include <stdint.h>

#include "CsvRow.h"
#include "LineQueue.h"
#include "OutputLine.h"

struct CsvPublisherCounters {
    uint32_t rows_queued = 0;
    uint32_t dropped_rows = 0;        // the CSV's output_dropped_rows: complete rows refused by a full queue
    uint32_t dropped_diagnostics = 0; // "# " lines refused by a full queue (not in the CSV count)
    uint32_t row_overflows = 0;       // rows that didn't fit an OutputLine (reported, never printed cut)
};

// "# <text>\r\n" as one line. If it doesn't fit, the line becomes a fixed
// "# ERROR: diagnostic line too long, not printed" instead (never cut short).
void formatDiagnostic(OutputLine& line, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void formatDiagnosticV(OutputLine& line, const char* fmt, va_list args);

class CsvPublisher {
  public:
    // row_capacity < OutputLine::kCapacity is only for testing overflow.
    CsvPublisher(LineQueue& queue, const SnapshotConfig& config, size_t row_capacity = OutputLine::kCapacity);

    // Queues the header. Only the first call does anything; returns false if
    // the header was already queued or the queue refused it.
    bool start();

    // Snapshot -> row -> queue. Take now_ms AFTER copying the inputs out of
    // their mailboxes. Returns the snapshot (for the "# calc" status line).
    DynoSnapshot publishRow(const MeasurementInputs& in, uint32_t now_ms);

    // Queues "# <text>". false = queue full (counted in dropped_diagnostics).
    bool diagnostic(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    const CsvPublisherCounters& counters() const { return counters_; }

  private:
    bool queueDiagnostic(const OutputLine& line);

    LineQueue& queue_;
    SnapshotConfig config_;
    size_t row_capacity_;
    bool started_ = false;
    CsvPublisherCounters counters_;
};
