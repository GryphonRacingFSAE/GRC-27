// A push button on an ESP32 GPIO (#36), read with digitalRead. The pin's
// internal pull resistor holds it at the "released" level: pull-up for an
// active-low button (wired to GND), pull-down for an active-high one (wired
// to 3.3 V). Debouncing and what a press does are TareButton's job.
#pragma once

#include <ButtonInput.h>

class GpioButton : public ButtonInput {
  public:
    GpioButton(int pin, bool active_low) : pin_(pin), active_low_(active_low) {}

    // Sets the pin to an input with the right pull. Call once before reading.
    void begin();

    bool levelHigh() override;

  private:
    int pin_;
    bool active_low_;
};
