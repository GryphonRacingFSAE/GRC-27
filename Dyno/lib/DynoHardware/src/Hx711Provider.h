// The real HX711 behind the LoadCellProvider interface (ESP32 only).
// Only the library's non-waiting calls are used; see docs/loadcell-acquisition.md.
#pragma once

#include <HX711.h>
#include <LoadCellProvider.h>

class Hx711Provider : public LoadCellProvider {
  public:
    Hx711Provider(int dout_pin, int sck_pin);

    // Call once, from the task, before the first isReady().
    void begin();

    bool isReady() override;
    int32_t readOnce() override;
    void powerCycle() override;

  private:
    HX711 hx711_;
    int dout_pin_;
    int sck_pin_;
};
