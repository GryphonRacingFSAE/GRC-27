#include <LoadCellTask.h>

#include <API.h>
#include <DynoCalibrationSetup.h>
#include <DynoConfig.h>
#include <LoadCellCalibrator.h>
#include <PreferencesCalibrationStore.h>
#include <pindefs.h>

#if DYNO_FAKE_INPUTS
#include <FakeLoadCellProvider.h>
#else
#include <Hx711Provider.h>
#endif

// The NVS namespace the calibration is saved under. The fake-inputs build
// uses its own, so trying the commands on a devkit never overwrites the
// calibration of a board that has been on the rig.
#if DYNO_FAKE_INPUTS
static constexpr const char* kCalibrationNamespace = "dyno-fake";
#else
static constexpr const char* kCalibrationNamespace = "dyno";
#endif

// Calibration replies become "# " lines (OutputTask prints them).
class DiagnosticReplies : public CalibrationReplies {
  public:
    void reply(const char* text) override { printDiagnostic("%s", text); }
};

static bool sameCounters(const LoadCellCounters& a, const LoadCellCounters& b) {
    return a.timeouts == b.timeouts && a.faults_entered == b.faults_entered &&
           a.recoveries == b.recoveries && a.samples == b.samples;
}

void LoadCellTask(void* pvParameters) {
    const LoadCellTaskParams* params = static_cast<const LoadCellTaskParams*>(pvParameters);

#if DYNO_FAKE_INPUTS
    FakeLoadCellProvider fake(demoLoadCellScript());
    LoadCellProvider& provider = fake;
#else
    Hx711Provider hx711(PIN_HX711_DOUT, PIN_HX711_SCK);
    hx711.begin();
    LoadCellProvider& provider = hx711;
#endif

    const LoadCellAcquirerConfig config = {HX711_READY_DEADLINE_MS, HX711_RETRY_LIMIT,
                                           HX711_RECOVERY_INTERVAL_MS};
    LoadCellAcquirer acquirer(provider, config);
    LoadCellCounters published_counters = acquirer.counters();

    // Static, not on this task's stack: the stability window alone is ~1 KB.
    // Only this task ever touches them.
    static PreferencesCalibrationStore store(kCalibrationNamespace);
    static DiagnosticReplies replies;
    static LoadCellCalibrator calibrator(store, replies, dynoCalibratorConfig());
    calibrator.begin(); // loads the saved calibration and says what it found
    setForceCalibrated(calibrator.calibrated());

    for (;;) {
        const uint32_t now_ms = millis();
#if DYNO_FAKE_INPUTS
        fake.setTime(now_ms);
#endif
        if (acquirer.step(now_ms)) {
            const LoadCellSample sample = acquirer.sample();
            xQueueOverwrite(params->sampleMailbox, &sample);
        }

        // Operator commands: take whatever is waiting, never wait for more.
        LoadCellCommand command;
        while (xQueueReceive(params->commandQueue, &command, 0) == pdTRUE) {
            calibrator.command(command, now_ms);
        }

        // Every pass, new sample or not: the calibrator also needs the time
        // to notice a timeout. The force carries the versions it was made
        // with, so it can never be paired with another calibration.
        const ForceMeasurement force = calibrator.step(acquirer.sample(), now_ms);
        xQueueOverwrite(params->forceMailbox, &force);
        setForceCalibrated(calibrator.calibrated());

        const LoadCellCounters counters = acquirer.counters();
        if (!sameCounters(counters, published_counters)) {
            xQueueOverwrite(params->countersMailbox, &counters);
            published_counters = counters;
        }

        vTaskDelay(pdMS_TO_TICKS(HX711_POLL_INTERVAL_MS));
    }
}
