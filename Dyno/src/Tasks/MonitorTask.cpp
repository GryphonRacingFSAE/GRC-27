#include <MonitorTask.h>

#include <API.h>
#include <CanReceiver.h>
#include <DynoCalcSetup.h>
#include <DynoConfig.h>
#include <LoadCellAcquirer.h>
#include <TorquePower.h>
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

// Decimal places that show a value at its DBC resolution (0.001 -> 3).
static int decimalsFor(double scale) {
    int d = 0;
    while (d < 6 && scale < 0.999999) {
        scale *= 10;
        ++d;
    }
    return d;
}

// Appends to buf[0..size), never overflowing; returns the new length.
static size_t append(char* buf, size_t size, size_t len, const char* fmt, ...) {
    if (len >= size) {
        return len;
    }
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(buf + len, size - len, fmt, args);
    va_end(args);
    return n < 0 ? len : len + static_cast<size_t>(n);
}

static void printLoadCell(const MonitorTaskParams* params) {
    LoadCellSample sample;
    LoadCellCounters counters;
    xQueuePeek(params->loadCellSampleMailbox, &sample, 0);     // every mailbox is filled
    xQueuePeek(params->loadCellCountersMailbox, &counters, 0); // at creation, never empty

    Serial.printf("# loadcell raw=%ld t=%lu status=%s timeouts=%lu faults=%lu recoveries=%lu samples=%lu\r\n",
                  static_cast<long>(sample.raw), static_cast<unsigned long>(sample.timestamp_ms),
                  statusName(sample.status), static_cast<unsigned long>(counters.timeouts),
                  static_cast<unsigned long>(counters.faults_entered),
                  static_cast<unsigned long>(counters.recoveries), static_cast<unsigned long>(counters.samples));
}

// "# ecu recv=0x7F fresh=0x7F EngineSpeed=6000 Lambda_Average=1.000 ..."
// ("-" = that message never received).
static void printEcu(const MonitorTaskParams* params) {
    EcuState ecu;
    xQueuePeek(params->ecuMailbox, &ecu, 0);
    const uint32_t now_ms = millis(); // after the peek, so ages are never negative

    char line[512];
    size_t len = append(line, sizeof(line), 0, "# ecu recv=0x%02X fresh=0x%02X", ecu.receivedMask(),
                        ecu.freshMask(now_ms, CAN_FRESHNESS_MS));
    for (const EcuSignalDef& sig : kEcuSignals) {
        if (ecu.message(sig.message).received) {
            len = append(line, sizeof(line), len, " %s=%.*f", sig.name, decimalsFor(sig.scale),
                         ecu.value(sig.signal));
        } else {
            len = append(line, sizeof(line), len, " %s=-", sig.name);
        }
    }
    append(line, sizeof(line), len, "\r\n");
    Serial.print(line);
}

// "# can link=Running decoded=... hz=50,50,... unknown=0x5F0,x18FF0001"
// hz = frames per second of each message since the last line, in mask order.
static void printCan(const MonitorTaskParams* params, uint32_t (&prev_frames)[kEcuMessageCount],
                     uint32_t& prev_ms) {
    CanStatus s;
    xQueuePeek(params->canStatusMailbox, &s, 0);
    const uint32_t now_ms = millis();
    const uint32_t dt_ms = now_ms - prev_ms;

    char line[512];
    size_t len = append(line, sizeof(line), 0,
                        "# can link=%s decoded=%lu ignored=%lu malformed=%lu rx_errors=%lu missed=%lu "
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
    append(line, sizeof(line), len, "\r\n");
    Serial.print(line);
}

// "# calc torque_nm=25.000 power_w=1570.8 kw=1.571 hp=2.106 reverse_torque=0 versions=2/3 force_ts_ms=.. rpm_ts_ms=.."
// An invalid value prints as "-(Reason)".
static void printCalc(const MonitorTaskParams* params) {
    EcuState ecu;
    xQueuePeek(params->ecuMailbox, &ecu, 0);
    // No calibrated force exists until #35 (calibration) produces one, so
    // force is honestly "unavailable" here: torque/power read ForceUnavailable.
    const ForceMeasurement force;
    const uint32_t now_ms = millis(); // after the peek, so ages are never negative

    const TorquePowerResult r =
        computeTorquePower(force, engineSpeedFromEcu(ecu), dynoMechanicalSetup(), dynoCalcLimits(), now_ms);

    char line[256];
    size_t len = append(line, sizeof(line), 0, "# calc");
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
    len = append(line, sizeof(line), len, " force_ts_ms=%lu rpm_ts_ms=%lu", static_cast<unsigned long>(r.force_timestamp_ms),
                 static_cast<unsigned long>(r.rpm_timestamp_ms));
    append(line, sizeof(line), len, "\r\n");
    Serial.print(line);
}

void MonitorTask(void* pvParameters) {
    const MonitorTaskParams* params = static_cast<const MonitorTaskParams*>(pvParameters);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_diagnostics_ms = millis();
    uint32_t prev_frames[kEcuMessageCount] = {};
    uint32_t prev_can_ms = millis();

    for (;;) {
        printLoadCell(params);
        printEcu(params);
        printCan(params, prev_frames, prev_can_ms);
        printCalc(params);

        // Boot messages are often missed (USB serial connects late): repeat them.
        const uint32_t now_ms = millis();
        if (now_ms - last_diagnostics_ms >= DIAGNOSTIC_REPEAT_INTERVAL_MS) {
            printDiagnosticWarnings();
            last_diagnostics_ms = now_ms;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SERIAL_MONITOR_INTERVAL_MS));
    }
}
