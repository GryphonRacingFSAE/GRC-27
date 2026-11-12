#include <LoadCellTask.h>

#include <DynoConfig.h>
#include <pindefs.h>

#if DYNO_FAKE_INPUTS
#include <FakeLoadCellProvider.h>
#else
#include <Hx711Provider.h>
#endif

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

    for (;;) {
        const uint32_t now_ms = millis();
#if DYNO_FAKE_INPUTS
        fake.setTime(now_ms);
#endif
        if (acquirer.step(now_ms)) {
            const LoadCellSample sample = acquirer.sample();
            xQueueOverwrite(params->sampleMailbox, &sample);
        }

        const LoadCellCounters counters = acquirer.counters();
        if (!sameCounters(counters, published_counters)) {
            xQueueOverwrite(params->countersMailbox, &counters);
            published_counters = counters;
        }

        vTaskDelay(pdMS_TO_TICKS(HX711_POLL_INTERVAL_MS));
    }
}
