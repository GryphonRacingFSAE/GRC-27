// The path every sample takes. See ImuPipeline.h.

#include <Imu/ImuPipeline.h>
#include <Imu/ImuQuality.h>

void applyRawSample(ImuSnapshot& snapshot, const RawImuSample& raw, const AxisMapping& mapping, uint32_t acquired_ms) {
    if (!raw.accelRead) {
        markFailed(snapshot.accel, ImuStatus::ReadFailed);
    } else if (raw.accelSaturated) {
        markFailed(snapshot.accel, ImuStatus::Saturated);
    } else {
        // Convert to g once, then turn into car directions.
        SensorVector accel_g = {mgToG(raw.accel_mg.x), mgToG(raw.accel_mg.y), mgToG(raw.accel_mg.z)};
        VehicleVector car = mapSensorToVehicle(accel_g, mapping);
        acceptReading(snapshot.accel, car.longitudinal, car.lateral, car.vertical, acquired_ms);
    }

    if (!raw.gyroRead) {
        markFailed(snapshot.gyro, ImuStatus::ReadFailed);
    } else if (raw.gyroSaturated) {
        markFailed(snapshot.gyro, ImuStatus::Saturated);
    } else {
        // Already in deg/s, so no unit conversion. The three values come out as
        // roll, pitch and yaw rate.
        VehicleVector car = mapSensorToVehicle(raw.gyro_dps, mapping);
        acceptReading(snapshot.gyro, car.longitudinal, car.lateral, car.vertical, acquired_ms);
    }
}
