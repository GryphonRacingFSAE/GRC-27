// ON-DEVICE test (#36): the REAL GpioButton + TareButton on an ESP32-S3,
// with the real pin and settings from pindefs.h / DynoConfig.h.
//
// Setup: an ESP32-S3 devkit. With PIN_TARE_BUTTON = 0 (today's placeholder)
// the button is the devkit's BOOT button; once the proto board's button is
// wired, set the real pin and use that button instead.
// Run:   pio test -e esp32-s3-devboard -f test_device_tare_button
// Build only (no hardware): add --without-uploading --without-testing
//
// INTERACTIVE: when the monitor says "PRESS the tare button", press it once,
// firmly, within 10 s, then let go. Don't touch it otherwise. Do NOT hold it
// while the board resets: on GPIO0 that starts the chip in download mode.
//
// This proves the pin number, the pull resistor and the active_low setting
// (the button idles "released" and a press reads "pressed"), and that one
// real press with real contact bounce gives exactly one Tare. It also prints
// how much the button bounced; write that in docs/calibration.md. It does
// NOT prove the calibrator's tare (host tests do) or the proto board wiring
// if it is run on a bare devkit.
#include <Arduino.h>
#include <unity.h>

#include <DynoCalibrationSetup.h>
#include <DynoConfig.h>
#include <FakeCommands.h>
#include <GpioButton.h>
#include <TareButton.h>
#include <pindefs.h>

static GpioButton pin(PIN_TARE_BUTTON, TARE_BUTTON_ACTIVE_LOW);

// Shows every line the button said.
static void printReplies(FakeReplies& replies) {
    for (const std::string& line : replies.lines) TEST_MESSAGE(line.c_str());
}

void setUp() {}
void tearDown() {}

// Untouched for 1 s, read every 1 ms: always "released". Fails if the pin
// number is wrong, the pull is wrong, or active_low is backwards.
static void test_idle_button_reads_released() {
    uint32_t pressed_reads = 0;
    for (int i = 0; i < 1000; ++i) {
        if (pressedFromLevel(pin.levelHigh(), TARE_BUTTON_ACTIVE_LOW)) ++pressed_reads;
        delay(1);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, pressed_reads,
                                     "button reads pressed while untouched: check PIN_TARE_BUTTON, "
                                     "TARE_BUTTON_ACTIVE_LOW and the wiring");
}

// One real press, polled like CommandTask, gives exactly one Tare and one
// "button: tare pressed" line; nothing more in the 2 s after release.
static void test_one_real_press_is_one_tare() {
    FakeCommandSink sink;
    FakeReplies replies;
    TareButton button(pin, sink, replies, dynoTareButtonConfig());

    TEST_MESSAGE("PRESS the tare button once now (10 s)...");
    const uint32_t start = millis();
    while (sink.queued.empty() && millis() - start < 10000) {
        button.step(millis());
        delay(COMMAND_POLL_INTERVAL_MS);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, sink.queued.size(), "no press seen within 10 s");

    TEST_MESSAGE("...release it and leave it for 5 s");
    const uint32_t after = millis();
    while (millis() - after < 5000) {
        button.step(millis());
        delay(COMMAND_POLL_INTERVAL_MS);
    }
    printReplies(replies);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, sink.queued.size(), "one press gave more than one tare: debounce too short?");
    TEST_ASSERT_EQUAL_UINT32(1, sink.count(CommandKind::Tare));
    TEST_ASSERT_EQUAL_UINT32(1, replies.lines.size());
}

// How much the contacts really chatter: poll every 1 ms through one press
// and release and report the flickers. Informational (it passes either
// way, but a press must be seen); record the numbers in docs/calibration.md.
static void test_measure_bounce() {
    ButtonDebouncer d(1); // accept anything that lasts 1 ms: counts raw flickers
    uint32_t changes = 0;
    bool last = pressedFromLevel(pin.levelHigh(), TARE_BUTTON_ACTIVE_LOW);
    uint32_t first_ms = 0, last_ms = 0;
    TEST_MESSAGE("PRESS and release the tare button once more (10 s)...");
    const uint32_t start = millis();
    while (millis() - start < 10000) {
        const bool now = pressedFromLevel(pin.levelHigh(), TARE_BUTTON_ACTIVE_LOW);
        d.step(now, millis());
        if (now != last) {
            if (changes == 0) first_ms = millis();
            last_ms = millis();
            ++changes;
            last = now;
        }
        if (d.presses() > 0 && !now && millis() - last_ms > 500) break; // released and settled
        delay(1);
    }
    char msg[120];
    snprintf(msg, sizeof msg, "level changes=%lu (2 = no bounce) over %lu ms; 1 ms-debounced flickers=%lu",
             (unsigned long)changes, (unsigned long)(last_ms - first_ms), (unsigned long)d.bounces());
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(changes >= 2, "no press seen within 10 s");
}

void setup() {
    delay(2000); // let the USB serial monitor attach
    pin.begin();
    delay(10);   // let the pull resistor settle
    UNITY_BEGIN();
    RUN_TEST(test_idle_button_reads_released);
    RUN_TEST(test_one_real_press_is_one_tare);
    RUN_TEST(test_measure_bounce);
    UNITY_END();
}

void loop() {}
