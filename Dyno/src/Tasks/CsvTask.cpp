#include <CsvTask.h>

#include <API.h>
#include <CanReceiver.h>
#include <CsvPublisher.h>
#include <DynoCalcSetup.h>
#include <DynoConfig.h>
#include <LoadCellAcquirer.h>
#include <OutputTask.h>
#include <stdarg.h>
#include <stdio.h>

static const char* statusName(LoadCellStatus status) {
    switch (status) {
    case LoadCellStatus::NoSampleYet:
        return "NoSampleYet";
    case LoadCellStatus::Valid:
        return "Valid";
    case LoadCellStatus::Fault:
        return "Fault";
    }
    return "?";
}

static const char* linkName(CanLinkStatus link) {
    switch (link) {
    case CanLinkStatus::Starting:
        return "Starting";
    case CanLinkStatus::Running:
        return "Running";
    case CanLinkStatus::StartFailed:
        return "StartFailed";
    case CanLinkStatus::Recovering:
        return "Recovering";
    }
    return "?";
}

// Appends to buf[0..size), never overflowing; returns the new length
// (>= size means it didn't fit).
static size_t append(char* buf, size_t size, size_t len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t append(char* buf, size_t size, size_t len, const char* fmt, ...) {
    if (len >= size) {
        return len;
    }
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf + len, size - len, fmt, args);
    va_end(args);
    return n < 0 ? size : len + static_cast<size_t>(n);
}

// A status line built in pieces. A cut-short line is never printed.
static void queueBuilt(CsvPublisher& pub, const char* line, size_t len, size_t size, const char* what) {
    if (len >= size) {
        pub.diagnostic("ERROR: %s status line too long, not printed", what);
    } else {
        pub.diagnostic("%s", line);
    }
}

// "# loadcell raw=.. t=.. status=Valid timeouts=.. faults=.. recoveries=.. samples=.."
static void loadCellStatusLine(CsvPublisher& pub, const CsvTaskParams* params) {
    LoadCellSample sample;
    LoadCellCounters counters;
    xQueuePeek(params->loadCellSampleMailbox, &sample, 0);     // every mailbox is filled
    xQueuePeek(params->loadCellCountersMailbox, &counters, 0); // at creation, never empty
    pub.diagnostic("loadcell raw=%ld t=%lu status=%s timeouts=%lu faults=%lu recoveries=%lu samples=%lu",
                   static_cast<long>(sample.raw), static_cast<unsigned long>(sample.timestamp_ms),
                   statusName(sample.status), static_cast<unsigned long>(counters.timeouts),
                   static_cast<unsigned long>(counters.faults_entered), static_cast<unsigned long>(counters.recoveries),
                   static_cast<unsigned long>(counters.samples));
}

// "# can link=Running decoded=... hz=50,50,... unknown=0x5F0,x18FF0001"
// hz = frames per second of each message since the last line, in mask order.
static void canStatusLine(CsvPublisher& pub, const CsvTaskParams* params, uint32_t (&prev_frames)[kEcuMessageCount],
                          uint32_t& prev_ms) {
    CanStatus s;
    xQueuePeek(params->canStatusMailbox, &s, 0);
    const uint32_t now_ms = millis();
    const uint32_t dt_ms = now_ms - prev_ms;

    char line[448];
    size_t len = append(line, sizeof(line), 0,
                        "can link=%s decoded=%lu ignored=%lu malformed=%lu rx_errors=%lu missed=%lu "
                        "bus_errors=%lu rec=%lu tec=%lu%s bus_off=%lu recoveries=%lu recovery_failures=%lu "
                        "start_failures=%lu hz=",
                        linkName(s.link), static_cast<unsigned long>(s.frames_decoded),
                        static_cast<unsigned long>(s.frames_ignored), static_cast<unsigned long>(s.frames_malformed),
                        static_cast<unsigned long>(s.rx_errors), static_cast<unsigned long>(s.rx_missed),
                        static_cast<unsigned long>(s.bus_errors), static_cast<unsigned long>(s.rx_error_counter),
                        static_cast<unsigned long>(s.tx_error_counter), s.error_passive ? " ERROR_PASSIVE" : "",
                        static_cast<unsigned long>(s.bus_off_events), static_cast<unsigned long>(s.recoveries),
                        static_cast<unsigned long>(s.recovery_failures), static_cast<unsigned long>(s.start_failures));
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        const uint32_t hz = dt_ms ? (s.message_frames[m] - prev_frames[m]) * 1000u / dt_ms : 0;
        len = append(line, sizeof(line), len, m ? ",%lu" : "%lu", static_cast<unsigned long>(hz));
        prev_frames[m] = s.message_frames[m];
    }
    prev_ms = now_ms;

    len = append(line, sizeof(line), len, " unknown=");
    if (s.unknown_id_count == 0) {
        len = append(line, sizeof(line), len, "none");
    }
    for (uint8_t i = 0; i < s.unknown_id_count; ++i) {
        const uint32_t id = s.unknown_ids[i];
        if (id & kExtendedIdFlag) {
            len = append(line, sizeof(line), len, i ? ",x%08lX" : "x%08lX",
                         static_cast<unsigned long>(id & ~kExtendedIdFlag));
        } else {
            len = append(line, sizeof(line), len, i ? ",0x%03lX" : "0x%03lX", static_cast<unsigned long>(id));
        }
    }
    if (s.unknown_ids_overflowed) {
        len = append(line, sizeof(line), len, ",+more");
    }
    queueBuilt(pub, line, len, sizeof(line), "can");
}

// "# calc torque_nm=-(ForceUnavailable) power_w=-(ForceUnavailable) reverse_torque=0 versions=- ..."
// From the same snapshot as the row just queued; "-(Reason)" says why a
// value is blank in the CSV.
static void calcStatusLine(CsvPublisher& pub, const DynoSnapshot& snap) {
    const TorquePowerResult& r = snap.calc;
    char line[256];
    size_t len = append(line, sizeof(line), 0, "calc");
    if (r.torque.valid) {
        len = append(line, sizeof(line), len, " torque_nm=%.3f", r.torque.torque_nm);
    } else {
        len = append(line, sizeof(line), len, " torque_nm=-(%s)", calcReasonName(r.torque.reason));
    }
    if (r.power.valid) {
        len = append(line, sizeof(line), len, " power_w=%.1f kw=%.3f hp=%.3f", r.power.power_w, r.power.power_kw,
                     r.power.horsepower_hp);
    } else {
        len = append(line, sizeof(line), len, " power_w=-(%s)", calcReasonName(r.power.reason));
    }
    len = append(line, sizeof(line), len, " reverse_torque=%d", r.reverse_torque ? 1 : 0);
    if (r.versions_known) {
        len = append(line, sizeof(line), len, " versions=%lu/%lu", static_cast<unsigned long>(r.calibration_version),
                     static_cast<unsigned long>(r.tare_version));
    } else {
        len = append(line, sizeof(line), len, " versions=-");
    }
    len = append(line, sizeof(line), len, " force_ts_ms=%lu rpm_ts_ms=%lu",
                 static_cast<unsigned long>(r.force_timestamp_ms), static_cast<unsigned long>(r.rpm_timestamp_ms));
    queueBuilt(pub, line, len, sizeof(line), "calc");
}

// "# output rows_dropped=.. diagnostics_dropped=.. row_overflows=.. queue_free=.."
static void outputStatusLine(CsvPublisher& pub, const FreeRtosLineQueue& queue) {
    const CsvPublisherCounters& c = pub.counters();
    pub.diagnostic("output rows_dropped=%lu diagnostics_dropped=%lu row_overflows=%lu queue_free=%lu/%lu",
                   static_cast<unsigned long>(c.dropped_rows),
                   static_cast<unsigned long>(c.dropped_diagnostics + diagnosticLinesDropped()),
                   static_cast<unsigned long>(c.row_overflows), static_cast<unsigned long>(queue.spacesLeft()),
                   static_cast<unsigned long>(CSV_OUTPUT_QUEUE_LINES));
}

void CsvTask(void* pvParameters) {
    const CsvTaskParams* params = static_cast<const CsvTaskParams*>(pvParameters);

    FreeRtosLineQueue queue(params->outputQueue);
    CsvPublisher publisher(queue, dynoSnapshotConfig());

    bool started = false;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_status_ms = millis();
    uint32_t last_warnings_ms = millis();
    uint32_t prev_frames[kEcuMessageCount] = {};
    uint32_t prev_can_ms = millis();

    for (;;) {
        // The header goes first, once. (The queue is empty at boot, so this
        // succeeds at once; if not, rows wait rather than come headerless.)
        if (!started && publisher.start()) {
            started = true;
            printDiagnosticWarnings();
        }

        if (started) {
            // Copy each producer's latest state (no locks held while printing).
            MeasurementInputs inputs;
            xQueuePeek(params->ecuMailbox, &inputs.ecu, 0);
            xQueuePeek(params->loadCellSampleMailbox, &inputs.loadcell, 0);
            // inputs.force stays "unavailable" until #35 (calibration)
            // publishes a ForceMeasurement: force/torque/power stay blank.
            const uint32_t now_ms = millis(); // AFTER the peeks, so ages are never negative

            const DynoSnapshot snap = publisher.publishRow(inputs, now_ms);

            if (now_ms - last_status_ms >= STATUS_LINE_INTERVAL_MS) {
                loadCellStatusLine(publisher, params);
                canStatusLine(publisher, params, prev_frames, prev_can_ms);
                calcStatusLine(publisher, snap);
                outputStatusLine(publisher, queue);
                last_status_ms = now_ms;
            }
            // Boot messages are often missed (USB serial connects late): repeat them.
            if (now_ms - last_warnings_ms >= DIAGNOSTIC_REPEAT_INTERVAL_MS) {
                printDiagnosticWarnings();
                last_warnings_ms = now_ms;
            }
        }

        // A fixed rate, not "100 ms after we finished": rows don't drift.
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CSV_ROW_INTERVAL_MS));
    }
}
