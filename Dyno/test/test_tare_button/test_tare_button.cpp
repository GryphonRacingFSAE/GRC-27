// The tare button end to end on the host (#36): a scripted button (the pin
// level at each poll) -> TareButton -> a bounded command queue -> the real
// LoadCellCalibrator, stepped the way CommandTask (button, every 20 ms) and
// LoadCellTask (commands + samples, every 5 ms) do on the ESP32.
#include <unity.h>

#include "CalibrationExamples.h"
#include "FakeButton.h"
#include "FakeCalibrationStore.h"
#include "FakeCommands.h"
#include "TareButton.h"
#include "TareButtonExamples.h"

void setUp() {}
void tearDown() {}

// FIXTURE: active-low (pull-up, button to ground), 50 ms debounce.
static const TareButtonConfig kActiveLow = kExampleTareButtonConfig;
static constexpr uint32_t kPollMs = 20; // CommandTask's poll interval

// Button only (no calibrator): what goes into the queue and what is said.
struct Panel {
    FakeButton pin;
    FakeCommandSink sink;
    FakeReplies replies;
    TareButton button{pin, sink, replies, kActiveLow};

    // Polls every 20 ms from `from` to before `to` with the pin at `high`.
    void poll(bool high, uint32_t from, uint32_t to) {
        pin.high = high;
        for (uint32_t t = from; t < to; t += kPollMs) button.step(t);
    }
};

static void test_a_press_queues_one_tare_and_says_so() {
    Panel p;
    p.poll(true, 0, 1000);   // released (pulled up)
    p.poll(false, 1000, 3000); // pressed and held 2 s
    p.poll(true, 3000, 4000);

    TEST_ASSERT_EQUAL_UINT32(1, p.sink.queued.size());
    TEST_ASSERT_TRUE(p.sink.queued.front().kind == CommandKind::Tare);
    TEST_ASSERT_EQUAL_UINT32(1, p.replies.lines.size());
    TEST_ASSERT_EQUAL_STRING("button: tare pressed (presses=1 bounces=0)", p.replies.lines[0].c_str());
}

// The queue is full (the load-cell task is stuck or slow): the press can't
// be delivered, so the operator is told to press again instead of thinking
// it worked. Nothing is retried behind their back.
static void test_a_press_into_a_full_queue_says_it_was_dropped() {
    Panel p;
    p.sink.capacity = 0;
    p.poll(true, 0, 1000);
    p.poll(false, 1000, 1500);
    p.poll(true, 1500, 3000);

    TEST_ASSERT_EQUAL_UINT32(0, p.sink.queued.size());
    TEST_ASSERT_EQUAL_UINT32(1, p.replies.lines.size());
    TEST_ASSERT_EQUAL_STRING("button: tare dropped (too many commands at once); press again", p.replies.lines[0].c_str());
    TEST_ASSERT_EQUAL_UINT32(1, p.button.presses()); // it was still a press
}

// The whole path on one fake clock. Every 5 ms (LoadCellTask's loop) the
// queue is drained into the calibrator and the latest sample is fed; a new
// HX711 sample arrives every 100 ms (10 Hz); the button is polled every
// 20 ms (CommandTask). Typed commands go into the same queue, as on the ESP32.
struct Rig {
    FakeButton pin;
    FakeCommandSink queue;
    FakeReplies replies;
    FakeCalibrationStore store;
    LoadCellCalibrator cal{store, replies, kExampleCalibratorConfig};
    TareButton button;
    int32_t raw = 100000; // what the load cell reads (counts)
    uint32_t now = 0;     // the next time run() will step

    bool pressed_high; // the pin level that means "pressed" for this wiring
    ForceMeasurement last; // the force from the latest step

    explicit Rig(const TareButtonConfig& config = kActiveLow)
        : button(pin, queue, replies, config), pressed_high(!config.active_low) {
        pin.high = !pressed_high; // released
        cal.begin();
        replies.lines.clear(); // drop the boot report
    }

    // Steps every 5 ms up to (not including) `to`, keeping pin and raw as set.
    void runUntil(uint32_t to) {
        for (; now < to; now += 5) {
            if (now % kPollMs == 0) button.step(now);
            while (!queue.queued.empty()) {
                cal.command(queue.queued.front(), now);
                queue.queued.pop_front();
            }
            LoadCellSample s;
            s.status = LoadCellStatus::Valid;
            s.raw = raw;
            s.timestamp_ms = now - now % 100;
            last = cal.step(s, now);
        }
    }

    void type(const char* line) {
        const ParseResult p = parseCommand(line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("None", parseErrorName(p.error), line);
        TEST_ASSERT_TRUE(queue.send(p.command));
    }

    // The worked example (#35): unloaded 100000, 100 N reads 120000, so
    // 200 counts/N. Accepted by t = 6100, then 1 s idle at 100000.
    void calibrate() {
        raw = 100000;
        type("cal unloaded");
        runUntil(1200);
        type("cal loaded 100N");
        raw = 120000;
        runUntil(6100);
        TEST_ASSERT_TRUE_MESSAGE(cal.calibrated(), "worked-example calibration not accepted");
        raw = 100000;
        runUntil(7000);
        replies.lines.clear();
    }

    // Press for `ms` then release, from the current time.
    void press(uint32_t ms) {
        pin.high = pressed_high;
        runUntil(now + ms);
        pin.high = !pressed_high;
    }
};

static void test_a_press_on_a_calibrated_rig_tares_it() {
    Rig r;
    r.calibrate();
    r.raw = 101000; // the zero has drifted 1000 counts (5 N)
    r.runUntil(8000);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, r.last.force_n);

    r.pin.high = false; // pressed at 8000 and still held at 8300...
    r.runUntil(8300);
    TEST_ASSERT_EQUAL_UINT32(1, r.button.presses()); // ...so it counted on the press, not the release
    r.pin.high = true;
    r.runUntil(10000);

    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("button: tare pressed (presses=1 bounces=0)"));
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_DOUBLE(101000.0, r.cal.active().offset_counts);
    TEST_ASSERT_EQUAL_UINT32(1, r.cal.active().calibration_version);
    TEST_ASSERT_EQUAL_UINT32(2, r.cal.active().tare_version);
    TEST_ASSERT_TRUE(r.last.calibration_ready);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, r.last.force_n);
    TEST_ASSERT_EQUAL_UINT32(2, r.last.tare_version);

    r.raw = 111000; // 10000 counts above the new zero = 50 N
    r.runUntil(10200);
    TEST_ASSERT_EQUAL_DOUBLE(50.0, r.last.force_n);
}

// No calibration yet: the button gets the same refusal as typing "tare".
static void test_a_press_before_calibration_is_refused() {
    Rig r;
    r.runUntil(1000);
    r.press(300);
    r.runUntil(3000);

    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("button: tare pressed"));
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare: refused, not calibrated"));
    TEST_ASSERT_TRUE(r.cal.phase() == CalibratorPhase::Idle);
    TEST_ASSERT_EQUAL_UINT32(0, r.cal.active().tare_version);
    TEST_ASSERT_FALSE(r.last.available);
}

// Pressed in the middle of a calibration (someone leaning on the panel):
// refused as busy, and the calibration carries on untouched.
static void test_a_press_during_a_calibration_is_busy_and_does_not_disturb_it() {
    Rig r;
    r.calibrate();
    r.type("cal unloaded");
    r.runUntil(7100);
    r.press(300);
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare: busy"));
    TEST_ASSERT_TRUE(r.cal.phase() == CalibratorPhase::CollectingUnloaded);
    r.runUntil(9000);
    TEST_ASSERT_TRUE(r.cal.phase() == CalibratorPhase::AwaitingLoad);
    TEST_ASSERT_EQUAL_UINT32(1, r.cal.active().tare_version);
}

// Holding the button for 5 s, through the whole 1 s tare and after it, is
// one tare: it must not start a second one when the first finishes.
static void test_holding_the_button_through_the_tare_is_one_tare() {
    Rig r;
    r.calibrate();
    r.raw = 101000;
    r.press(5000);
    r.runUntil(14000);

    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("button: tare pressed"));
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT32(2, r.cal.active().tare_version);
}

// An impatient second press while the first tare is still collecting is
// "busy"; the first tare still finishes normally. A press after it is done
// is a new tare.
static void test_a_second_press_during_the_tare_is_busy_then_a_later_one_tares_again() {
    Rig r;
    r.calibrate();
    r.raw = 101000;
    r.press(200);   // 7000: tare starts at ~7060
    r.runUntil(7400);
    r.press(200);   // ~7460: the tare is still collecting
    r.runUntil(9000);
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare: busy"));
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT32(2, r.cal.active().tare_version);

    r.raw = 102000;
    r.press(200);
    r.runUntil(11000);
    TEST_ASSERT_EQUAL_UINT32(2, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT32(3, r.cal.active().tare_version);
    TEST_ASSERT_EQUAL_DOUBLE(102000.0, r.cal.active().offset_counts);
    TEST_ASSERT_TRUE(r.replies.count("presses=3 bounces=0") == 1);
}

// Contact chatter seen by the 20 ms poll: pressed at 7000, released 7020,
// pressed 7040, released 7060, then steady from 7080. Two flickers undone
// (bounces=2); one press, accepted at the first poll 50 ms after 7080 = 7140.
static void test_a_bouncy_press_is_one_tare_and_the_line_shows_the_bounces() {
    Rig r;
    r.calibrate();
    r.raw = 101000;
    const bool chatter_pressed[] = {true, false, true, false}; // 7000..7060
    for (bool pressed : chatter_pressed) {
        r.pin.high = pressed ? r.pressed_high : !r.pressed_high;
        r.runUntil(r.now + kPollMs);
    }
    r.pin.high = r.pressed_high; // steady from 7080
    r.runUntil(7140);
    TEST_ASSERT_EQUAL_UINT32(0, r.replies.count("button: tare pressed"));
    r.runUntil(7145); // the 7140 poll
    TEST_ASSERT_EQUAL_UINT32(1, r.button.presses());
    r.runUntil(7500);
    r.pin.high = !r.pressed_high;
    r.runUntil(9000);

    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("button: tare pressed (presses=1 bounces=2)"));
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT32(2, r.cal.active().tare_version);
}

// A button held at power-up (or stuck/shorted, or wired with the wrong
// active_low setting so "released" reads as pressed) never tares on its own.
static void test_a_button_held_from_boot_does_not_tare() {
    Rig r;
    r.pin.high = r.pressed_high; // pressed before the first poll
    r.calibrate();
    r.raw = 101000;
    r.runUntil(15000);
    TEST_ASSERT_EQUAL_UINT32(0, r.replies.count("button:"));
    TEST_ASSERT_EQUAL_UINT32(0, r.button.presses());
    TEST_ASSERT_EQUAL_UINT32(1, r.cal.active().tare_version);
}

// Active-high wiring (button to 3.3 V, pull-down): HIGH is pressed. Same
// behaviour, mirrored; and the pin idling LOW is not a press.
static void test_active_high_wiring_tares_on_high() {
    Rig r({false, 50});
    TEST_ASSERT_FALSE(r.pin.high); // idles LOW
    r.calibrate();
    r.raw = 101000;
    r.pin.high = true; // pressed at 7000 and still held at 7300...
    r.runUntil(7300);
    TEST_ASSERT_EQUAL_UINT32(1, r.button.presses()); // ...so it counted on the press, not the release
    r.pin.high = false;
    r.runUntil(10000);
    TEST_ASSERT_EQUAL_UINT32(1, r.button.presses());
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("button: tare pressed (presses=1 bounces=0)"));
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT32(2, r.cal.active().tare_version);
}

// The queue is full at the moment of the press (here: the operator just
// typed four commands): the press is dropped and the operator told; the
// typed commands still run and nothing tares later by surprise.
static void test_a_dropped_press_never_tares_later() {
    Rig r;
    r.calibrate();
    r.raw = 101000;
    r.pin.high = r.pressed_high;
    r.runUntil(7060); // up to the poll that accepts the press
    for (int i = 0; i < 4; ++i) r.type("cal status");
    r.runUntil(7065);
    TEST_ASSERT_EQUAL_UINT32(1, r.replies.count("button: tare dropped (too many commands at once); press again"));
    r.pin.high = !r.pressed_high;
    r.runUntil(12000);
    TEST_ASSERT_EQUAL_UINT32(0, r.replies.count("tare OK"));
    TEST_ASSERT_EQUAL_UINT32(1, r.cal.active().tare_version);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_press_queues_one_tare_and_says_so);
    RUN_TEST(test_a_press_into_a_full_queue_says_it_was_dropped);
    RUN_TEST(test_a_press_on_a_calibrated_rig_tares_it);
    RUN_TEST(test_a_press_before_calibration_is_refused);
    RUN_TEST(test_a_press_during_a_calibration_is_busy_and_does_not_disturb_it);
    RUN_TEST(test_holding_the_button_through_the_tare_is_one_tare);
    RUN_TEST(test_a_second_press_during_the_tare_is_busy_then_a_later_one_tares_again);
    RUN_TEST(test_a_bouncy_press_is_one_tare_and_the_line_shows_the_bounces);
    RUN_TEST(test_a_button_held_from_boot_does_not_tare);
    RUN_TEST(test_active_high_wiring_tares_on_high);
    RUN_TEST(test_a_dropped_press_never_tares_later);
    return UNITY_END();
}
