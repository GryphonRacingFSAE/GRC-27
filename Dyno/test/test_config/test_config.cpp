// Invariants on the hardware placeholders. These must keep holding when the
// TBD values in pindefs.h / DynoConfig.h are replaced with real ones.
#include <unity.h>

#include "DynoConfig.h"
#include "pindefs.h"

void setUp() {}
void tearDown() {}

static void test_all_dyno_pins_are_distinct() {
    TEST_ASSERT_TRUE(dynoPinsAreDistinct());
}

// HX711 runs at 10 Hz or 80 Hz depending on its RATE pin; the readiness
// deadline must cover a full 10 Hz conversion period (100 ms).
static void test_hx711_deadline_covers_slowest_sample_rate() {
    TEST_ASSERT_GREATER_THAN_UINT32(100, HX711_READY_DEADLINE_MS);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_dyno_pins_are_distinct);
    RUN_TEST(test_hx711_deadline_covers_slowest_sample_rate);
    return UNITY_END();
}
