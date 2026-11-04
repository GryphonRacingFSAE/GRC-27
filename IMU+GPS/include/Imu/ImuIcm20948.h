/* The real IMU: a SparkFun ICM-20948 on SPI. The pins are in pindefs.h.
 * Settings: accel +/-8 g, gyro +/-500 deg/s, about 100 Hz.
 *
 * TODO(#40): the sensor and bus are ASSUMED. Riyan to confirm them and the wiring.
 */

#ifndef IMU_ICM20948_H
#define IMU_ICM20948_H

#include <Imu/ImuPipeline.h>
#include <stdint.h>

// SPI speed. 4 MHz is the library's default; the chip allows up to 7 MHz.
constexpr uint32_t IMU_SPI_FREQ_HZ = 4000000;

/* One attempt to start the sensor. Returns true if it worked.
 * NOTE: takes roughly 50-200 ms. That is estimated from the library code, not
 * measured. Call it from the IMU task, not setup(), and don't retry in a tight
 * loop. imuRunnerStep handles retries.
 */
bool imuIcmInit();

/* Reads the sensor, but only if it has a new sample.
 * Returns false if there is nothing new. Returns true if a read was attempted:
 * "out" holds the sample, or accelRead/gyroRead = false if the read failed.
 */
bool imuIcmPoll(RawImuSample& out);

#endif // IMU_ICM20948_H
