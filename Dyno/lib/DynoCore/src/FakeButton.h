// A button whose pin level a host test sets by hand (#36). Starts HIGH,
// which is "released" for the real active-low (pull-up) wiring.
#pragma once

#include "ButtonInput.h"

class FakeButton : public ButtonInput {
  public:
    bool high = true;
    bool levelHigh() override { return high; }
};
