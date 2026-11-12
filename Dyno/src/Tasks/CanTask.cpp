#include <CanTask.h>

#include <DynoConfig.h>
#include <pindefs.h>

#if DYNO_FAKE_INPUTS
#include <FakeCanBus.h>
#else
#include <TwaiCanBus.h>
#endif

void CanTask(void* pvParameters) {
    const CanTaskParams* params = static_cast<const CanTaskParams*>(pvParameters);

#if DYNO_FAKE_INPUTS
    FakeCanBus fake(demoEcuScript());
    CanBus& bus = fake;
#else
    TwaiCanBus twai(PIN_CAN_TX, PIN_CAN_RX, CAN_BITRATE_KBPS, CAN_RX_QUEUE_LEN);
    CanBus& bus = twai;
#endif

    const CanReceiverConfig config = {CAN_RX_TIMEOUT_MS, CAN_RESTART_INTERVAL_MS};
    CanReceiver receiver(bus, config);

    for (;;) {
        const uint32_t now_ms = millis();
#if DYNO_FAKE_INPUTS
        fake.setTime(now_ms);
#endif
        // Waits at most CAN_RX_TIMEOUT_MS inside the driver.
        const CanStepResult result = receiver.step(now_ms);
        if (result == CanStepResult::Decoded) {
            xQueueOverwrite(params->ecuMailbox, &receiver.ecu());
        }
        xQueueOverwrite(params->statusMailbox, &receiver.status());

        // Nothing arrived (silent ECU, driver error, start failure, bus-off):
        // sleep so this task never spins. When frames are flowing, loop at
        // once to drain the driver's queue.
        if (result == CanStepResult::Idle) {
            vTaskDelay(pdMS_TO_TICKS(CAN_IDLE_DELAY_MS));
        }
    }
}
