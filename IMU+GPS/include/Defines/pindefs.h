#ifndef PIN_DEFS_H
#define PIN_DEFS_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// IMU on SPI (issue #41).
// TODO(#40): Riyan to confirm the wiring. CS = GPIO 10 was given. SCK, MOSI and
// MISO are ASSUMED: they are the ESP32-S3 default SPI pins that go with CS 10.
// Wrong pins -> imuIcmInit() keeps returning false, and this is the only place
// that needs changing.
// ---------------------------------------------------------------------------
constexpr uint8_t IMU_SPI_CS_PIN = 10;
constexpr uint8_t IMU_SPI_MOSI_PIN = 11;
constexpr uint8_t IMU_SPI_SCK_PIN = 12;
constexpr uint8_t IMU_SPI_MISO_PIN = 13;

#endif // PIN_DEFS_H