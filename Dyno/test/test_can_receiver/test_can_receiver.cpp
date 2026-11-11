// CAN reception (#33), tested with no hardware: a scripted FakeCanBus drives
// CanReceiver::step(now_ms) through simulated time, like the real task does.
#include <unity.h>

#include <stdio.h>

#include "CanReceiver.h"
#include "FakeCanBus.h"

void setUp() {}
void tearDown() {}

// The real defaults from DynoConfig.h.
static const CanReceiverConfig kConfig = {/*rx_timeout_ms=*/20, /*restart_interval_ms=*/1000};

static const uint32_t kFreshMs = 2000;
static const uint8_t kAllMessages = 0x7F;

static FakeEcuScript script(const FakeEcuPhase* phases, size_t count, uint32_t period_ms = 20,
                            uint32_t origin_ms = 0, uint32_t repeat_ms = 0) {
    return FakeEcuScript{phases, count, period_ms, origin_ms, repeat_ms};
}

// One task-loop iteration at now_ms: keep stepping while frames are waiting
// (the real task does the same), stop at the first Idle. Returns how many
// steps decoded an ECU frame.
static int pumpAt(FakeCanBus& bus, CanReceiver& rx, uint32_t now_ms) {
    bus.setTime(now_ms);
    int decoded = 0;
    for (int guard = 0; guard < 1000; ++guard) {
        const CanStepResult r = rx.step(now_ms);
        if (r == CanStepResult::Decoded) {
            ++decoded;
        } else if (r == CanStepResult::Idle) {
            return decoded;
        }
    }
    TEST_FAIL_MESSAGE("step() never went Idle");
    return decoded;
}

// Task loop every tick_ms over [from_ms, to_ms).
static void runFor(FakeCanBus& bus, CanReceiver& rx, uint32_t from_ms, uint32_t to_ms,
                   uint32_t tick_ms = 5) {
    for (uint32_t t = from_ms; t != to_ms; t += tick_ms) {
        pumpAt(bus, rx, t);
    }
}

// ---------------------------------------------------------------------------
// Seam C: CanReceiver::step with FakeCanBus
// ---------------------------------------------------------------------------

static void test_first_step_starts_the_bus_and_then_receives() {
    const FakeEcuPhase phases[] = {{0, 10000, kAllMessages}};
    FakeCanBus bus(script(phases, 1));
    CanReceiver rx(bus, kConfig);
    TEST_ASSERT_EQUAL(CanLinkStatus::Starting, rx.status().link);

    pumpAt(bus, rx, 0);

    TEST_ASSERT_EQUAL_UINT32(1, bus.starts());
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    // Slot 0 (t=0) carries all 7 messages.
    TEST_ASSERT_EQUAL_HEX8(0x7F, rx.ecu().receivedMask());
    TEST_ASSERT_EQUAL_UINT32(7, rx.status().frames_decoded);
    TEST_ASSERT_EQUAL_UINT32(0, bus.receivesWhileNotRunning());
}

// Frames are stamped with their arrival time, and decoded with the values the
// fake ECU sent (RPM = 1000 + t/2 at script time t).
static void test_frames_are_decoded_with_their_arrival_time() {
    const FakeEcuPhase phases[] = {{0, 10000, kAllMessages}};
    FakeCanBus bus(script(phases, 1, 20, /*origin_ms=*/5000));
    CanReceiver rx(bus, kConfig);

    runFor(bus, rx, 5000, 6005);

    // Last slot at or before 6000 ms is script time 1000 ms.
    TEST_ASSERT_EQUAL_UINT32(6000, rx.ecu().message(EcuMessage::Engine1).last_rx_ms);
    TEST_ASSERT_EQUAL_INT32(1500, rx.ecu().raw(EcuSignal::EngineSpeed));
    TEST_ASSERT_EQUAL_DOUBLE(1.000, rx.ecu().value(EcuSignal::LambdaAverage));
    TEST_ASSERT_EQUAL_DOUBLE(13.26, rx.ecu().value(EcuSignal::BatteryVoltage));
    // 51 slots (0, 20, ..., 1000 ms) x 7 messages.
    TEST_ASSERT_EQUAL_UINT32(51 * 7, rx.status().frames_decoded);
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        TEST_ASSERT_EQUAL_UINT32(51, rx.status().message_frames[m]);
    }
}

// Driver install/start can fail (wrong pins, out of memory). Retry once per
// restart_interval_ms, never spin, never receive from a stopped driver.
static void test_failed_start_is_retried_every_interval() {
    const FakeEcuPhase phases[] = {{0, 100000, kAllMessages}};
    FakeCanBus bus(script(phases, 1));
    bus.failNextStarts(2);
    CanReceiver rx(bus, kConfig);

    runFor(bus, rx, 0, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, bus.starts());
    TEST_ASSERT_EQUAL(CanLinkStatus::StartFailed, rx.status().link);
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().start_failures);

    runFor(bus, rx, 1000, 2000);
    TEST_ASSERT_EQUAL_UINT32(2, bus.starts());
    TEST_ASSERT_EQUAL_UINT32(2, rx.status().start_failures);
    TEST_ASSERT_EQUAL(CanLinkStatus::StartFailed, rx.status().link);
    TEST_ASSERT_EQUAL_HEX8(0x00, rx.ecu().receivedMask()); // nothing invented

    runFor(bus, rx, 2000, 2005);
    TEST_ASSERT_EQUAL_UINT32(3, bus.starts());
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    TEST_ASSERT_EQUAL_HEX8(0x7F, rx.ecu().receivedMask());
    TEST_ASSERT_EQUAL_UINT32(0, bus.receivesWhileNotRunning());
}

// No ECU on the bus: every step waits (bounded) and reports Idle, the link
// is still Running, and no values are made up.
static void test_silent_bus_waits_bounded_and_invents_nothing() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);

    for (uint32_t t = 0; t < 5000; t += 5) {
        bus.setTime(t);
        TEST_ASSERT_EQUAL(CanStepResult::Idle, rx.step(t));
    }
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    TEST_ASSERT_EQUAL_HEX8(0x00, rx.ecu().receivedMask());
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().frames_decoded);
    TEST_ASSERT_EQUAL_UINT32(1000, bus.receiveCalls());
    TEST_ASSERT_EQUAL_UINT32(kConfig.rx_timeout_ms, bus.longestTimeoutMs());
}

// Retry timing must survive millis() wrapping during the wait.
static void test_start_retry_across_millis_rollover() {
    FakeCanBus bus(script(nullptr, 0));
    bus.failNextStarts(1);
    CanReceiver rx(bus, kConfig);

    bus.setTime(0xFFFFFE00u); // 512 ms before the wrap
    rx.step(0xFFFFFE00u);
    TEST_ASSERT_EQUAL_UINT32(1, bus.starts());

    bus.setTime(0x000001E7u); // 999 ms later
    rx.step(0x000001E7u);
    TEST_ASSERT_EQUAL_UINT32(1, bus.starts());

    bus.setTime(0x000001E8u); // 1000 ms later
    rx.step(0x000001E8u);
    TEST_ASSERT_EQUAL_UINT32(2, bus.starts());
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
}

// Each frame that isn't a good ECU frame is counted by kind, and changes
// nothing in the EcuState.
static void test_bad_and_foreign_frames_are_counted_not_decoded() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0); // start

    const uint8_t data[8] = {0x60, 0xEA, 0, 0, 0, 0, 0, 0};
    bus.inject(0x123, false, false, 8, data);      // someone else's message
    bus.inject(0x520, true, false, 8, data);       // extended ID: not ours
    bus.inject(0x520, false, true, 8, nullptr);    // remote request
    bus.inject(0x520, false, false, 4, data);      // our ID, wrong length
    bus.inject(0x521, false, false, 8, nullptr);   // our ID, no payload

    bus.setTime(10);
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL(CanStepResult::Frame, rx.step(10));
    }
    TEST_ASSERT_EQUAL(CanStepResult::Idle, rx.step(10));

    TEST_ASSERT_EQUAL_UINT32(3, rx.status().frames_ignored);
    TEST_ASSERT_EQUAL_UINT32(2, rx.status().frames_malformed);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().frames_decoded);
    TEST_ASSERT_EQUAL_HEX8(0x00, rx.ecu().receivedMask());
    TEST_ASSERT_EQUAL_INT32(0, rx.ecu().raw(EcuSignal::EngineSpeed));
}

// Unknown IDs are remembered (distinct, in order of first sighting), so a
// hardware session shows at once if the ECU uses different IDs than the DBC.
static void test_unknown_ids_are_listed_once_each() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);

    const uint8_t data[8] = {};
    bus.inject(0x5F0, false, false, 8, data);
    bus.inject(0x5F0, false, false, 8, data);      // repeat: listed once
    bus.inject(0x18FF0001, true, false, 8, data);  // extended: flagged
    bus.inject(0x520, false, true, 0, nullptr);    // remote of OUR id: not "unknown"
    pumpAt(bus, rx, 10);

    TEST_ASSERT_EQUAL_UINT8(2, rx.status().unknown_id_count);
    TEST_ASSERT_EQUAL_HEX32(0x5F0, rx.status().unknown_ids[0]);
    TEST_ASSERT_EQUAL_HEX32(0x18FF0001u | kExtendedIdFlag, rx.status().unknown_ids[1]);
    TEST_ASSERT_FALSE(rx.status().unknown_ids_overflowed);
    TEST_ASSERT_EQUAL_UINT32(4, rx.status().frames_ignored);
}

static void test_unknown_id_list_is_bounded() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);

    const uint8_t data[8] = {};
    for (uint32_t id = 0x100; id < 0x100 + kUnknownIdSlots; ++id) {
        bus.inject(id, false, false, 8, data);
    }
    pumpAt(bus, rx, 10);
    TEST_ASSERT_EQUAL_UINT8(kUnknownIdSlots, rx.status().unknown_id_count);
    TEST_ASSERT_FALSE(rx.status().unknown_ids_overflowed);

    bus.inject(0x100, false, false, 8, data); // already listed: no overflow
    pumpAt(bus, rx, 20);
    TEST_ASSERT_FALSE(rx.status().unknown_ids_overflowed);

    bus.inject(0x7FF, false, false, 8, data); // one too many
    pumpAt(bus, rx, 30);
    TEST_ASSERT_EQUAL_UINT8(kUnknownIdSlots, rx.status().unknown_id_count);
    TEST_ASSERT_TRUE(rx.status().unknown_ids_overflowed);
    TEST_ASSERT_EQUAL_HEX32(0x107, rx.status().unknown_ids[kUnknownIdSlots - 1]);
}

// Driver receive errors are counted; the step goes Idle (caller sleeps).
static void test_receive_errors_are_counted_and_recovered_from() {
    const FakeEcuPhase phases[] = {{0, 10000, kAllMessages}};
    FakeCanBus bus(script(phases, 1));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    const uint32_t decoded_before = rx.status().frames_decoded;

    bus.failNextReceives(3);
    bus.setTime(20);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(CanStepResult::Idle, rx.step(20));
    }
    TEST_ASSERT_EQUAL_UINT32(3, rx.status().rx_errors);
    TEST_ASSERT_EQUAL_UINT32(decoded_before, rx.status().frames_decoded);

    pumpAt(bus, rx, 20); // errors over: the 20 ms slot arrives
    TEST_ASSERT_EQUAL_UINT32(decoded_before + 7, rx.status().frames_decoded);
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
}

// Frames lost because the driver's queue overflowed are reported as-is.
static void test_rx_queue_overflow_is_reported() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().rx_missed);

    CanBusErrors e;
    e.rx_missed = 5;
    bus.setErrors(e);
    pumpAt(bus, rx, 5);
    TEST_ASSERT_EQUAL_UINT32(5, rx.status().rx_missed);
}

// The controller's own error counters are reported, so a wrong bit rate or
// bad wiring (errors on every frame) looks different from a silent ECU.
static void test_controller_error_counters_are_reported() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().bus_errors);
    TEST_ASSERT_FALSE(rx.status().error_passive);

    CanBusErrors e;
    e.bus_errors = 57;
    e.rx_error_counter = 96;
    bus.setErrors(e);
    pumpAt(bus, rx, 10);
    TEST_ASSERT_EQUAL_UINT32(57, rx.status().bus_errors);
    TEST_ASSERT_EQUAL_UINT32(96, rx.status().rx_error_counter);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().tx_error_counter);
    TEST_ASSERT_FALSE(rx.status().error_passive);
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
}

// CAN spec: a controller is "error passive" once either error counter
// reaches 128 (it has seen so many errors it stops flagging them loudly).
static void test_error_passive_at_128() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    CanBusErrors e;

    e.rx_error_counter = 127;
    bus.setErrors(e);
    pumpAt(bus, rx, 10);
    TEST_ASSERT_FALSE(rx.status().error_passive);

    e.rx_error_counter = 128;
    bus.setErrors(e);
    pumpAt(bus, rx, 20);
    TEST_ASSERT_TRUE(rx.status().error_passive);

    e.rx_error_counter = 0;
    e.tx_error_counter = 128;
    bus.setErrors(e);
    pumpAt(bus, rx, 30);
    TEST_ASSERT_TRUE(rx.status().error_passive);

    e.tx_error_counter = 0;
    bus.setErrors(e);
    pumpAt(bus, rx, 40);
    TEST_ASSERT_FALSE(rx.status().error_passive);
}

// Counters keep updating while the link is not Running (recovering, or
// failing to start), so the monitor never shows frozen numbers.
static void test_counters_refresh_while_not_running() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    bus.goBusOff();
    pumpAt(bus, rx, 10);
    TEST_ASSERT_EQUAL(CanLinkStatus::Recovering, rx.status().link);

    CanBusErrors e;
    e.rx_missed = 3;
    e.bus_errors = 200;
    e.tx_error_counter = 255;
    bus.setErrors(e);
    pumpAt(bus, rx, 20);
    TEST_ASSERT_EQUAL_UINT32(3, rx.status().rx_missed);
    TEST_ASSERT_EQUAL_UINT32(200, rx.status().bus_errors);
    TEST_ASSERT_EQUAL_UINT32(255, rx.status().tx_error_counter);

    FakeCanBus bus2(script(nullptr, 0));
    bus2.failNextStarts(5);
    CanReceiver rx2(bus2, kConfig);
    pumpAt(bus2, rx2, 0);
    TEST_ASSERT_EQUAL(CanLinkStatus::StartFailed, rx2.status().link);
    e = CanBusErrors{};
    e.bus_errors = 9;
    bus2.setErrors(e);
    pumpAt(bus2, rx2, 10);
    TEST_ASSERT_EQUAL_UINT32(9, rx2.status().bus_errors);
}

// Bus-off: ask for recovery ONCE, leave the driver alone while it recovers,
// then restart it. Old values are kept (not zeroed) but go stale normally.
static void test_bus_off_recovers_and_restarts() {
    const FakeEcuPhase phases[] = {{0, 100000, kAllMessages}};
    FakeCanBus bus(script(phases, 1));
    CanReceiver rx(bus, kConfig);
    runFor(bus, rx, 0, 1005);
    const int32_t rpm_before = rx.ecu().raw(EcuSignal::EngineSpeed);
    TEST_ASSERT_EQUAL_INT32(1500, rpm_before);

    bus.goBusOff();
    const uint32_t receives_before = bus.receiveCalls();
    runFor(bus, rx, 1005, 4000);

    TEST_ASSERT_EQUAL_UINT32(1, rx.status().bus_off_events);
    TEST_ASSERT_EQUAL_UINT32(1, bus.recoveryRequests());
    TEST_ASSERT_EQUAL(CanLinkStatus::Recovering, rx.status().link);
    TEST_ASSERT_EQUAL_UINT32(receives_before, bus.receiveCalls()); // left alone
    TEST_ASSERT_EQUAL_INT32(rpm_before, rx.ecu().raw(EcuSignal::EngineSpeed));
    TEST_ASSERT_EQUAL_HEX8(0x7F, rx.ecu().receivedMask());
    TEST_ASSERT_EQUAL_HEX8(0x00, rx.ecu().freshMask(4000, kFreshMs)); // stale, honestly

    bus.finishRecovery(); // driver: Recovering -> Stopped
    pumpAt(bus, rx, 4000);
    TEST_ASSERT_EQUAL_UINT32(2, bus.starts());
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().recoveries);
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    TEST_ASSERT_EQUAL_HEX8(0x7F, rx.ecu().freshMask(4000, kFreshMs));
    TEST_ASSERT_EQUAL_UINT32(0, bus.receivesWhileNotRunning());
}

static void test_repeated_bus_off_is_counted_each_time() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    for (uint32_t i = 1; i <= 3; ++i) {
        bus.goBusOff();
        pumpAt(bus, rx, i * 100);
        pumpAt(bus, rx, i * 100 + 5);
        bus.finishRecovery();
        pumpAt(bus, rx, i * 100 + 10);
        TEST_ASSERT_EQUAL_UINT32(i, rx.status().bus_off_events);
        TEST_ASSERT_EQUAL_UINT32(i, rx.status().recoveries);
        TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    }
    TEST_ASSERT_EQUAL_UINT32(3, bus.recoveryRequests());
}

// If the restart after recovery fails, fall back to the normal retry loop.
static void test_failed_restart_after_recovery_is_retried() {
    FakeCanBus bus(script(nullptr, 0));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    bus.goBusOff();
    pumpAt(bus, rx, 100);
    bus.finishRecovery();
    bus.failNextStarts(1);

    pumpAt(bus, rx, 200);
    TEST_ASSERT_EQUAL(CanLinkStatus::StartFailed, rx.status().link);
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().start_failures);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().recoveries); // not running again yet

    runFor(bus, rx, 205, 1200);
    TEST_ASSERT_EQUAL_UINT32(2, bus.starts()); // no retry before 1 s
    pumpAt(bus, rx, 1200);
    TEST_ASSERT_EQUAL_UINT32(3, bus.starts());
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().recoveries);
}

// If the driver refuses to start recovery, the bus stays off: ask again
// every restart_interval_ms (never spin, never give up), and count refusals.
static void test_refused_recovery_is_retried_not_stuck() {
    const FakeEcuPhase phases[] = {{0, 100000, kAllMessages}};
    FakeCanBus bus(script(phases, 1));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);

    bus.refuseNextRecoveries(2);
    bus.goBusOff();
    runFor(bus, rx, 100, 1100); // first request at 100 ms: refused
    TEST_ASSERT_EQUAL_UINT32(1, bus.recoveryRequests());
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().recovery_failures);
    TEST_ASSERT_EQUAL(CanLinkStatus::Recovering, rx.status().link);

    runFor(bus, rx, 1100, 2100); // retried at 1100 ms: refused again
    TEST_ASSERT_EQUAL_UINT32(2, bus.recoveryRequests());
    TEST_ASSERT_EQUAL_UINT32(2, rx.status().recovery_failures);

    pumpAt(bus, rx, 2100); // third request accepted
    TEST_ASSERT_EQUAL_UINT32(3, bus.recoveryRequests());
    TEST_ASSERT_EQUAL(CanBusState::Recovering, bus.state());
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().bus_off_events); // still ONE bus-off

    bus.finishRecovery();
    pumpAt(bus, rx, 2200);
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().recoveries);
    TEST_ASSERT_EQUAL_HEX8(0x7F, rx.ecu().freshMask(2200, 2000));
    TEST_ASSERT_EQUAL_UINT32(0, bus.receivesWhileNotRunning());
}

// Defensive: if the driver reports Running again without passing through
// Stopped, carry on receiving instead of waiting in Recovering forever.
static void test_bus_running_again_during_recovery_resumes() {
    const FakeEcuPhase phases[] = {{0, 100000, kAllMessages}};
    FakeCanBus bus(script(phases, 1));
    CanReceiver rx(bus, kConfig);
    pumpAt(bus, rx, 0);
    bus.goBusOff();
    pumpAt(bus, rx, 100);
    TEST_ASSERT_EQUAL(CanLinkStatus::Recovering, rx.status().link);

    bus.start(); // something else restarted the controller
    pumpAt(bus, rx, 200);
    pumpAt(bus, rx, 220);
    TEST_ASSERT_EQUAL(CanLinkStatus::Running, rx.status().link);
    TEST_ASSERT_EQUAL_UINT32(1, rx.status().recoveries);
    TEST_ASSERT_EQUAL_UINT32(220, rx.ecu().message(EcuMessage::Engine1).last_rx_ms);
}

// ---------------------------------------------------------------------------
// Seam D: the demo sequence (#38 builds its CSV against this, with no ECU)
// ---------------------------------------------------------------------------

struct DemoCheck {
    uint32_t at_ms;
    uint8_t received;
    uint8_t fresh;
};

// Expected masks from the documented timeline (50 Hz slots every 20 ms):
//   0-2 s silent, 2-10 s all, 10-15 s no 0x530, 15-20 s silent, 20-25 s all,
//   then repeat. A message goes stale 2000 ms after its last frame.
static void test_demo_sequence_timeline() {
    FakeCanBus bus(demoEcuScript());
    CanReceiver rx(bus, kConfig);
    const DemoCheck checks[] = {
        {0, 0x00, 0x00},      // startup: nothing yet
        {1995, 0x00, 0x00},
        {2000, 0x7F, 0x7F},   // normal updates
        {9995, 0x7F, 0x7F},
        {11975, 0x7F, 0x7F},  // 0x530 last at 9980: age 1995, still fresh
        {11980, 0x7F, 0x5F},  // ... age 2000: stale (missing frames)
        {14995, 0x7F, 0x5F},
        {16975, 0x7F, 0x5F},  // the rest last at 14980
        {16980, 0x7F, 0x00},  // all stale
        {19995, 0x7F, 0x00},  // still "received": a stale value is not "never seen"
        {20000, 0x7F, 0x7F},  // recovery
        {26975, 0x7F, 0x7F},  // cycle 2 starts silent at 25000
        {26980, 0x7F, 0x00},
        {27000, 0x7F, 0x7F},  // and comes back at 2 s into the cycle
    };
    uint32_t t = 0;
    for (const DemoCheck& c : checks) {
        for (; t <= c.at_ms; t += 5) {
            pumpAt(bus, rx, t);
        }
        char msg[48];
        snprintf(msg, sizeof(msg), "at %u ms", static_cast<unsigned>(c.at_ms));
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(c.received, rx.ecu().receivedMask(), msg);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(c.fresh, rx.ecu().freshMask(c.at_ms, kFreshMs), msg);
    }
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().frames_ignored);
    TEST_ASSERT_EQUAL_UINT32(0, rx.status().frames_malformed);
}

// The demo's values: RPM ramps 1000 + t/2 within each 25 s cycle; the rest
// are fixed and plausible.
static void test_demo_values() {
    FakeCanBus bus(demoEcuScript());
    CanReceiver rx(bus, kConfig);
    runFor(bus, rx, 0, 10005);

    const EcuState& e = rx.ecu();
    TEST_ASSERT_EQUAL_INT32(6000, e.raw(EcuSignal::EngineSpeed)); // t = 10000
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.000, e.value(EcuSignal::LambdaAverage));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.990, e.value(EcuSignal::LambdaA));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 1.010, e.value(EcuSignal::LambdaB));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 11.00, e.value(EcuSignal::FuelInjPulsewidth));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 45.0, e.value(EcuSignal::FuelInjDuty));
    TEST_ASSERT_EQUAL_INT32(0, e.raw(EcuSignal::KnockDetected));
    TEST_ASSERT_EQUAL_INT32(11000, e.raw(EcuSignal::RevLimitRpm));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.950, e.value(EcuSignal::LambdaTarget));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 13.26, e.value(EcuSignal::BatteryVoltage));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 34.6, e.value(EcuSignal::IntakeAirTemp));
    TEST_ASSERT_DOUBLE_WITHIN(1e-9, 88.5, e.value(EcuSignal::CoolantTemp));
    TEST_ASSERT_EQUAL_INT32(1, e.raw(EcuSignal::GearPosn));

    // 0x530 stopped at 9980 (well, 10 s): its values stay as last received.
    runFor(bus, rx, 10005, 14005);
    TEST_ASSERT_EQUAL_UINT32(9980, e.message(EcuMessage::Environment).last_rx_ms);
    TEST_ASSERT_EQUAL_INT32(8000, e.raw(EcuSignal::EngineSpeed)); // t = 14000
}

// Every frame the demo sends is accepted, at 50 Hz per message.
static void test_demo_rates() {
    FakeCanBus bus(demoEcuScript());
    CanReceiver rx(bus, kConfig);
    runFor(bus, rx, 0, 25000);
    // Messages sent in 2-10 s and 20-25 s: 13 s at 50 Hz = 650 each;
    // all but 0x530 also in 10-15 s: +250.
    for (size_t m = 0; m < kEcuMessageCount; ++m) {
        const uint32_t expected = (m == static_cast<size_t>(EcuMessage::Environment)) ? 650 : 900;
        TEST_ASSERT_EQUAL_UINT32(expected, rx.status().message_frames[m]);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_step_starts_the_bus_and_then_receives);
    RUN_TEST(test_frames_are_decoded_with_their_arrival_time);
    RUN_TEST(test_failed_start_is_retried_every_interval);
    RUN_TEST(test_silent_bus_waits_bounded_and_invents_nothing);
    RUN_TEST(test_start_retry_across_millis_rollover);
    RUN_TEST(test_bad_and_foreign_frames_are_counted_not_decoded);
    RUN_TEST(test_unknown_ids_are_listed_once_each);
    RUN_TEST(test_unknown_id_list_is_bounded);
    RUN_TEST(test_receive_errors_are_counted_and_recovered_from);
    RUN_TEST(test_rx_queue_overflow_is_reported);
    RUN_TEST(test_controller_error_counters_are_reported);
    RUN_TEST(test_error_passive_at_128);
    RUN_TEST(test_counters_refresh_while_not_running);
    RUN_TEST(test_bus_off_recovers_and_restarts);
    RUN_TEST(test_repeated_bus_off_is_counted_each_time);
    RUN_TEST(test_failed_restart_after_recovery_is_retried);
    RUN_TEST(test_refused_recovery_is_retried_not_stuck);
    RUN_TEST(test_bus_running_again_during_recovery_resumes);
    RUN_TEST(test_demo_sequence_timeline);
    RUN_TEST(test_demo_values);
    RUN_TEST(test_demo_rates);
    return UNITY_END();
}
