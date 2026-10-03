#include <Arduino.h>
#include <SPI.h>
#include <bme68x.h> 
#include <math.h>
#include <pindefs.h>


namespace 
{
constexpr uint32_t SPI_CLOCK_HZ = 100000; // DLVR: 50-200 kHz for all speed options.
constexpr uint32_t BME_TIMEOUT_MS = 500;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint16_t BME_HEATER_MS = 150;
constexpr float PA_PER_INH2O = 249.08891f;

const SPISettings sensorSettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0);
bme68x_dev bme = {};
bme68x_conf bmeConfig = {};
bool bmeReady = false;
uint32_t bmeOperationStarted = 0;

void deselectAll()
{
  digitalWrite(SPI_CS_DLVR1, HIGH);
  digitalWrite(SPI_CS_DLVR2, HIGH);
  digitalWrite(SPI_CS_BME680, HIGH);
}

void selectSensor(int8_t cs)
{
  deselectAll();
  SPI.beginTransaction(sensorSettings);
  delayMicroseconds(3); // DLVR minimum CS-high time is 2 us.
  digitalWrite(cs, LOW);
  delayMicroseconds(3); // DLVR CS-to-clock setup: >=2.5 us.
}

void releaseSensor(int8_t cs)
{
  delayMicroseconds(1);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

void readDlvr(const char *name, int8_t cs)
{
  selectSensor(cs);
  SPI.transfer(0x00); 
  releaseSensor(cs);
  delay(20); 

  uint8_t raw[4] = {};
  uint8_t status = 0;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    selectSensor(cs);
    for (uint8_t &byte : raw) {
      byte = SPI.transfer(0x00);
    }
    releaseSensor(cs);
    status = raw[0] >> 6;
    if (status != 2 || attempt == 2) {
      break;
    }
    delay(20); // Retry stale data, with a fixed limit so a fault cannot stall loop().
  }

  Serial.printf("%s CS=%d raw=%02X %02X %02X %02X status=%u ",
                name, cs, raw[0], raw[1], raw[2], raw[3], status);
  const bool allZero = !(raw[0] | raw[1] | raw[2] | raw[3]);
  const bool allOnes = (raw[0] & raw[1] & raw[2] & raw[3]) == 0xFF;
  if (allZero || allOnes) {
    Serial.println("NO VALID FRAME: all 00/FF; check power, CS, MISO, and SPI part option.");
    return;
  }
  if (status != 0) {
    const char *errors[] = {"", "RESERVED STATUS", "STALE: no fresh conversion",
                            "FAULT: sensor electrical/configuration error"};
    Serial.println(errors[status]);
    return;
  }

  const uint16_t pressureCounts = (uint16_t(raw[0] & 0x3F) << 8) | raw[1];
  const uint16_t temperatureCounts = (uint16_t(raw[2]) << 3) | (raw[3] >> 5);
  // Differential span is 60 inH2O, centered at 8192 counts (80% transfer span).
  const float pressureInH2O = 1.25f * (int32_t(pressureCounts) - 8192) * 60.0f / 16384.0f;
  const float temperatureC = temperatureCounts * 200.0f / 2047.0f - 50.0f;
  Serial.printf("FRESH dP=%+.3f inH2O (%+.1f Pa) T=%.2f C counts=%u/%u",
                pressureInH2O, pressureInH2O * PA_PER_INH2O, temperatureC,
                pressureCounts, temperatureCounts);
  if (fabsf(pressureInH2O) > 30.0f || temperatureC < -25.0f || temperatureC > 85.0f) {
    Serial.print(" [OUT OF RATED RANGE]");
  }
  Serial.println();
}

// Use Bosch's driver directly for calibration/page handling and gas status bits.
// A callback deadline also terminates its otherwise unbounded sleep-mode poll
// if MISO becomes stuck high. Do not restart this timer inside the callbacks.
BME68X_INTF_RET_TYPE bmeRead(uint8_t reg, uint8_t *data, uint32_t length, void *)
{
  if (uint32_t(millis() - bmeOperationStarted) >= BME_TIMEOUT_MS) {
    return -1;
  }
  selectSensor(SPI_CS_BME680);
  SPI.transfer(reg | 0x80);
  for (uint32_t i = 0; i < length; ++i) {
    data[i] = SPI.transfer(0x00);
  }
  releaseSensor(SPI_CS_BME680);
  return 0;
}

BME68X_INTF_RET_TYPE bmeWrite(uint8_t reg, const uint8_t *data, uint32_t length, void *)
{
  if (uint32_t(millis() - bmeOperationStarted) >= BME_TIMEOUT_MS) {
    return -1;
  }
  selectSensor(SPI_CS_BME680);
  SPI.transfer(reg & 0x7F);
  // Bosch already interleaves subsequent register addresses and values.
  for (uint32_t i = 0; i < length; ++i) {
    SPI.transfer(data[i]);
  }
  releaseSensor(SPI_CS_BME680);
  return 0;
}

void bmeDelay(uint32_t us, void *)
{
  if (us >= 1000) {
    delay(us / 1000);
  }
  delayMicroseconds(us % 1000);
}

bool checkBme(int8_t result, const char *operation)
{
  if (result == BME68X_OK) {
    return true;
  }
  Serial.printf("BME680 CS=%d ERROR during %s: driver=%d%s; will retry next cycle.\n",
                SPI_CS_BME680, operation, result,
                uint32_t(millis() - bmeOperationStarted) >= BME_TIMEOUT_MS ? " (timeout)" : "");
  bmeReady = false;
  return false;
}

bool initializeBme()
{
  bme = {};
  bme.intf = BME68X_SPI_INTF;
  bme.read = bmeRead;
  bme.write = bmeWrite;
  bme.delay_us = bmeDelay;
  bme.amb_temp = 25;
  bmeOperationStarted = millis();
  const int8_t result = bme68x_init(&bme);
  Serial.printf("BME680 CS=%d chip ID=0x%02X (expected 0x61)\n", SPI_CS_BME680, bme.chip_id);
  if (!checkBme(result, "initialization")) {
    return false;
  }

  bmeConfig.os_hum = BME68X_OS_2X;
  bmeConfig.os_pres = BME68X_OS_4X;
  bmeConfig.os_temp = BME68X_OS_8X;
  bmeConfig.filter = BME68X_FILTER_OFF;
  bmeConfig.odr = BME68X_ODR_NONE;
  if (!checkBme(bme68x_set_conf(&bmeConfig, &bme), "configuration")) {
    return false;
  }
  bme68x_heatr_conf heater = {};
  heater.enable = BME68X_ENABLE;
  heater.heatr_temp = 320;
  heater.heatr_dur = BME_HEATER_MS;
  bmeReady = checkBme(bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heater, &bme), "heater setup");
  return bmeReady;
}

void readBme()
{
  if (!bmeReady && !initializeBme()) {
    return;
  }
  bmeOperationStarted = millis();
  uint8_t chipId = 0;
  if (!checkBme(bme68x_get_regs(BME68X_REG_CHIP_ID, &chipId, 1, &bme), "chip ID read")) {
    return;
  }
  if (chipId != BME68X_CHIP_ID) {
    Serial.printf("BME680 CS=%d INVALID ID=0x%02X (expected 0x61); check power/SPI.\n",
                  SPI_CS_BME680, chipId);
    bmeReady = false;
    return;
  }
  if (!checkBme(bme68x_set_op_mode(BME68X_FORCED_MODE, &bme), "starting conversion")) {
    return;
  }
  const uint32_t conversionUs = bme68x_get_meas_dur(BME68X_FORCED_MODE, &bmeConfig, &bme);
  delay((conversionUs + 999) / 1000 + BME_HEATER_MS + 10);

  bme68x_data data = {};
  uint8_t count = 0;
  if (!checkBme(bme68x_get_data(BME68X_FORCED_MODE, &data, &count, &bme), "reading conversion")) {
    return;
  }
  if (count == 0 || !(data.status & BME68X_NEW_DATA_MSK)) {
    Serial.println("BME680 NO FRESH DATA; will reinitialize next cycle.");
    bmeReady = false;
    return;
  }

  // The bundled driver enables floating-point compensation: C, Pa, %RH, ohms.
  Serial.printf("BME680 CS=%d FRESH T=%.2f C P=%.2f hPa RH=%.2f %% status=0x%02X sample=%u ",
                SPI_CS_BME680, data.temperature, data.pressure / 100.0f,
                data.humidity, data.status, data.meas_index);
  const bool gasValid = data.status & BME68X_GASM_VALID_MSK;
  const bool heaterStable = data.status & BME68X_HEAT_STAB_MSK;
  if (gasValid && heaterStable) {
    if (isfinite(data.gas_resistance) && data.gas_resistance > 0.0f) {
      Serial.printf("gas=%.2f kOhm", data.gas_resistance / 1000.0f);
    } else {
      Serial.print("gas=INVALID VALUE");
    }
  } else {
    Serial.printf("gas=NOT READY (valid=%u heater_stable=%u)", gasValid, heaterStable);
  }
  if (!isfinite(data.temperature) || !isfinite(data.pressure) || !isfinite(data.humidity) ||
      data.temperature < -40.0f || data.temperature > 85.0f ||
      data.pressure < 30000.0f || data.pressure > 110000.0f ||
      data.humidity < 0.0f || data.humidity > 100.0f) {
    Serial.print(" [OUT OF RATED RANGE / INVALID VALUE]");
  }
  Serial.println();
}
} // namespace

void setup()
{
  // Disable every slave before enabling the shared clock/MOSI signals.
  for (const int8_t cs : {SPI_CS_DLVR1, SPI_CS_DLVR2, SPI_CS_BME680}) {
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
  }
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  Serial.begin(115200);
  const uint32_t serialStarted = millis();
  while (!Serial && uint32_t(millis() - serialStarted) < 3000) {
    delay(10); // USB CDC gets time to attach, but an absent monitor cannot block.
  }
  delay(50); // Also covers DLVR and BME680 power-up delays.
  Serial.println("\nAeroProbe sensor diagnostic: 115200 baud, SPI mode 0, 100 kHz");
  Serial.printf("SCK=%d MISO=%d MOSI=%d; CS: DLVR1=%d DLVR2=%d BME680=%d\n",
                SPI_SCK, SPI_MISO, SPI_MOSI, SPI_CS_DLVR1, SPI_CS_DLVR2, SPI_CS_BME680);
  Serial.println("DLVR dP should be near zero with both ports at equal pressure.");
  Serial.println("Fresh data shows communication; check response to a small pressure/temperature change.");
}

void loop()
{
  const uint32_t cycleStarted = millis();
  Serial.printf("\n--- %lu ms ---\n", static_cast<unsigned long>(cycleStarted));
  readDlvr("DLVR1", SPI_CS_DLVR1);
  readDlvr("DLVR2", SPI_CS_DLVR2);
  readBme();
  deselectAll();
  const uint32_t elapsed = millis() - cycleStarted;
  if (elapsed < SAMPLE_INTERVAL_MS) {
    delay(SAMPLE_INTERVAL_MS - elapsed);
  }
}
