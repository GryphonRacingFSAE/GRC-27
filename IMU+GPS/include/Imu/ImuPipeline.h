/* The one path every sample takes, from the real sensor or the fake:
 *   accel:          mg -> g -> car directions -> checked and stored
 *   gyro:           car directions -> checked and stored
 *   failed read:    marked ReadFailed, old value and time kept
 *   maxed-out read: marked Saturated, old value and time kept
 * Sharing this path means testing with the fake also tests the car's conversion.
 */

#ifndef IMU_PIPELINE_H
#define IMU_PIPELINE_H

#include <Imu/ImuTypes.h>
#include <Imu/ImuConversion.h>
#include <stdint.h>

// One attempt to read the sensor, before any conversion.
struct RawImuSample {
    bool accelRead;         // did the accel read work?
    bool gyroRead;          // did the gyro read work?
    SensorVector accel_mg;  // chip directions, milli-g
    SensorVector gyro_dps;  // chip directions, deg/s
    bool accelSaturated;    // did any accel axis hit its limit?
    bool gyroSaturated;     // did any gyro axis hit its limit?
};

/* Converts one raw sample and updates the snapshot. Accel and gyro are handled
 * separately, so one can fail while the other updates.
 * acquired_ms is when the sample was READ. Only call this after a read attempt.
 * NOTE: maxed-out values are not published, because their real size is unknown.
 * If #43 wants them sent anyway, change it here.
 */
void applyRawSample(ImuSnapshot& snapshot, const RawImuSample& raw, const AxisMapping& mapping, uint32_t acquired_ms);

#endif // IMU_PIPELINE_H
