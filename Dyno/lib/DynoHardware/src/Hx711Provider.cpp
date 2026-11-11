#include "Hx711Provider.h"

#include <Arduino.h>

// Channel A, gain 128: the HX711's power-on default, so a power-cycle
// never silently changes the gain.
static constexpr uint8_t HX711_GAIN = 128;

// The HX711 powers down once its clock pin has been high for > 60 us.
static constexpr uint32_t HX711_POWER_DOWN_HOLD_US = 100;

Hx711Provider::Hx711Provider(int dout_pin, int sck_pin) : dout_pin_(dout_pin), sck_pin_(sck_pin) {}

void Hx711Provider::begin() {
    hx711_.begin(static_cast<byte>(dout_pin_), static_cast<byte>(sck_pin_), HX711_GAIN);
    // The library leaves DOUT floating on ESP chips. With the internal pull-up
    // an unplugged sensor reads HIGH = "not ready" (-> timeouts -> Fault)
    // instead of random "ready" garbage. Must come after begin().
    pinMode(dout_pin_, INPUT_PULLUP);
}

bool Hx711Provider::isReady() { return hx711_.is_ready(); }

// Only called after isReady() returned true, so the library's internal
// wait_ready() returns immediately. Interrupts are off for ~50-60 us inside.
int32_t Hx711Provider::readOnce() { return static_cast<int32_t>(hx711_.read()); }

void Hx711Provider::powerCycle() {
    hx711_.power_down();
    delayMicroseconds(HX711_POWER_DOWN_HOLD_US);
    hx711_.power_up(); // wakes in channel A / gain 128, matching HX711_GAIN
}
