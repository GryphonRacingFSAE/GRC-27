// Tare-button debouncing (#36), tested with a fake clock: the level the
// button reads at each poll is scripted by hand, and every expected event
// time is worked out by hand from the 50 ms rule.
#include <unity.h>

#include "ButtonDebouncer.h"

void setUp() {}
void tearDown() {}

// FIXTURE: a press counts once the button has read "pressed" for 50 ms.
static constexpr uint32_t kDebounceMs = 50;

// Polls every period_ms from `from` (inclusive) to `to` (exclusive) with a
// fixed level. Returns how many press events came out.
static uint32_t hold(ButtonDebouncer& b, bool pressed, uint32_t from, uint32_t to, uint32_t period_ms) {
    uint32_t events = 0;
    for (uint32_t t = from; t != to; t += period_ms) {
        if (b.step(pressed, t)) ++events;
    }
    return events;
}

static void test_never_pressed_gives_no_event() {
    ButtonDebouncer b(kDebounceMs);
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 0, 10000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, b.presses());
    TEST_ASSERT_EQUAL_UINT32(0, b.bounces());
}

// Polled every 1 ms; pressed cleanly at 1000 ms. The first "pressed" poll
// is at 1000, so 50 ms of steady "pressed" is reached at 1050, not 1049.
static void test_clean_press_counts_exactly_at_the_debounce_time() {
    ButtonDebouncer b(kDebounceMs);
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 0, 1000, 1));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, true, 1000, 1050, 1)); // up to 1049
    TEST_ASSERT_TRUE(b.step(true, 1050));
    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
    TEST_ASSERT_EQUAL_UINT32(0, b.bounces());
}

// Holding the button down (10 s, polled every 20 ms like CommandTask) is one
// tare, not one every 50 ms.
static void test_holding_the_button_does_not_repeat() {
    ButtonDebouncer b(kDebounceMs);
    hold(b, false, 0, 1000, 20);
    TEST_ASSERT_EQUAL_UINT32(1, hold(b, true, 1000, 11000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 11000, 12000, 20));
    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
}

// Press 200 ms, release 200 ms, press again: two presses, at 1060 and 1460
// (20 ms polls: the first poll 50 ms after 1000 is 1060).
static void test_two_separate_presses_give_two_events() {
    ButtonDebouncer b(kDebounceMs);
    hold(b, false, 0, 1000, 20);
    for (uint32_t t = 1000; t < 1600; t += 20) {
        const bool pressed = t < 1200 || t >= 1400;
        const bool expect_event = t == 1060 || t == 1460;
        TEST_ASSERT_EQUAL_MESSAGE(expect_event, b.step(pressed, t), "event at the wrong poll");
    }
    TEST_ASSERT_EQUAL_UINT32(2, b.presses());
    TEST_ASSERT_EQUAL_UINT32(0, b.bounces());
}

// Contacts chatter for a few ms when pressed and released (polled every
// 1 ms so every flicker is seen). Press: P R P R P then steady from 1004, so
// the 50 ms count starts at 1004 -> one event at 1054. Release: R P R then
// steady from 2002 -> no event. A "bounce" is a change undone before it
// lasted 50 ms: 1000->P (undone at 1001), 1002->P (undone at 1003), and
// 2000->R (undone at 2001) = 3.
static void test_bouncing_contacts_give_one_press_timed_from_the_last_flicker() {
    ButtonDebouncer b(kDebounceMs);
    hold(b, false, 0, 1000, 1);
    const bool press_chatter[] = {true, false, true, false}; // 1000..1003
    uint32_t t = 1000;
    for (bool level : press_chatter) TEST_ASSERT_FALSE(b.step(level, t++));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, true, 1004, 1054, 1));
    TEST_ASSERT_TRUE(b.step(true, 1054));
    hold(b, true, 1055, 2000, 1);

    const bool release_chatter[] = {false, true}; // 2000..2001
    t = 2000;
    for (bool level : release_chatter) TEST_ASSERT_FALSE(b.step(level, t++));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 2002, 3000, 1));

    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
    TEST_ASSERT_EQUAL_UINT32(3, b.bounces());
}

// Already down at the first poll (held through boot, or a stuck/miswired
// button): not a press. Only a release then a new press counts.
static void test_button_held_at_boot_does_not_count_until_pressed_again() {
    ButtonDebouncer b(kDebounceMs);
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, true, 0, 2000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 2000, 2500, 20));
    TEST_ASSERT_EQUAL_UINT32(1, hold(b, true, 2500, 3000, 20));
    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
    TEST_ASSERT_EQUAL_UINT32(0, b.bounces());
}

// A release shorter than 50 ms (the finger slipping, 40 ms at 20 ms polls)
// isn't a release: still one press, and the dip is one bounce.
static void test_short_release_does_not_split_one_press() {
    ButtonDebouncer b(kDebounceMs);
    hold(b, false, 0, 1000, 20);
    TEST_ASSERT_EQUAL_UINT32(1, hold(b, true, 1000, 1500, 20));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 1500, 1540, 20)); // 1500, 1520
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, true, 1540, 3000, 20));
    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
    TEST_ASSERT_EQUAL_UINT32(1, b.bounces());
}

// Electrical noise that reads "pressed" for one poll only: nothing.
static void test_one_poll_glitch_is_ignored() {
    ButtonDebouncer b(kDebounceMs);
    hold(b, false, 0, 1000, 20);
    TEST_ASSERT_FALSE(b.step(true, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, false, 1020, 3000, 20));
    TEST_ASSERT_EQUAL_UINT32(0, b.presses());
    TEST_ASSERT_EQUAL_UINT32(1, b.bounces());
}

// millis() wraps to 0 after ~49.7 days. Pressed at 2^32 - 30 ms (polled
// every 1 ms): 50 ms later is 20 ms after the wrap.
static void test_press_across_the_millis_wraparound() {
    ButtonDebouncer b(kDebounceMs);
    const uint32_t press_ms = 0xFFFFFFFFu - 29; // 2^32 - 30
    hold(b, false, press_ms - 1000, press_ms, 1);
    TEST_ASSERT_EQUAL_UINT32(0, hold(b, true, press_ms, 20, 1)); // up to 19 after the wrap
    TEST_ASSERT_TRUE(b.step(true, 20));
    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
}

// The task can be late (a busy CPU). A press seen at 1000 and next looked at
// 100 ms later has lasted long enough: one event then, none after.
static void test_late_poll_still_gives_exactly_one_event() {
    ButtonDebouncer b(kDebounceMs);
    hold(b, false, 0, 1000, 20);
    TEST_ASSERT_FALSE(b.step(true, 1000));
    TEST_ASSERT_TRUE(b.step(true, 1100));
    TEST_ASSERT_FALSE(b.step(true, 1300));
    TEST_ASSERT_EQUAL_UINT32(1, b.presses());
}

// Active-low (pull-up resistor, button connects the pin to ground): LOW =
// pressed. Active-high (button connects the pin to 3.3 V): HIGH = pressed.
static void test_pin_level_to_pressed_for_both_wirings() {
    TEST_ASSERT_TRUE(pressedFromLevel(false, true));  // active-low, pin LOW
    TEST_ASSERT_FALSE(pressedFromLevel(true, true));  // active-low, pin HIGH
    TEST_ASSERT_TRUE(pressedFromLevel(true, false));  // active-high, pin HIGH
    TEST_ASSERT_FALSE(pressedFromLevel(false, false)); // active-high, pin LOW
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_never_pressed_gives_no_event);
    RUN_TEST(test_clean_press_counts_exactly_at_the_debounce_time);
    RUN_TEST(test_holding_the_button_does_not_repeat);
    RUN_TEST(test_two_separate_presses_give_two_events);
    RUN_TEST(test_bouncing_contacts_give_one_press_timed_from_the_last_flicker);
    RUN_TEST(test_button_held_at_boot_does_not_count_until_pressed_again);
    RUN_TEST(test_short_release_does_not_split_one_press);
    RUN_TEST(test_one_poll_glitch_is_ignored);
    RUN_TEST(test_press_across_the_millis_wraparound);
    RUN_TEST(test_late_poll_still_gives_exactly_one_event);
    RUN_TEST(test_pin_level_to_pressed_for_both_wirings);
    return UNITY_END();
}
