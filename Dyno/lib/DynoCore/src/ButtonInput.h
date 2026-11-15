// One push button's pin, read on demand (#36). On the ESP32 this is
// lib/DynoHardware/GpioButton (digitalRead); host tests use FakeButton.
#pragma once

class ButtonInput {
  public:
    virtual ~ButtonInput() = default;
    // The pin level right now: true = HIGH. Whether HIGH means "pressed"
    // depends on the wiring (see pressedFromLevel in ButtonDebouncer.h).
    virtual bool levelHigh() = 0;
};
