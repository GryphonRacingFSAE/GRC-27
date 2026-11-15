#include "GpioButton.h"

#include <Arduino.h>

void GpioButton::begin() {
    pinMode(pin_, active_low_ ? INPUT_PULLUP : INPUT_PULLDOWN);
}

bool GpioButton::levelHigh() {
    return digitalRead(pin_) == HIGH;
}
