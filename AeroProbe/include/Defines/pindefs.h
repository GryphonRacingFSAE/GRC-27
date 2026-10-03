#pragma once

#include "Arduino.h"
#include "stdint.h"

// SPI USED BY ALL SENSORS, TOGGLE CS PINS FOR EACH SENSOR
static constexpr int8_t SPI_MOSI = 11;
static constexpr int8_t SPI_SCK  = 12;
static constexpr int8_t SPI_MISO = 13;

static constexpr int8_t SPI_CS_BME680 = 18;
static constexpr int8_t SPI_CS_DLVR1  = 15;
static constexpr int8_t SPI_CS_DLVR2  = 16;
