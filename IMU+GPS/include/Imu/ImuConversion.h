/* Turns sensor readings into the units and directions we want.
 *   Units: the library gives accel in milli-g, and we want g (1000 mg = 1 g).
 *   Axes:  the chip's X/Y/Z depend on how it is mounted, so a small table says
 *          which chip axis is forward, left and up.
 *
 * NOTE: SparkFun library v1.3.2 gives accel in mg and gyro in deg/s, so accel is
 * converted once and gyro not at all. Re-check if the library or sensor changes.
 * Converting twice makes values 1000x too small.
 * NOTE: that library returns 0, not an error, if its range setting is unknown.
 * A 0 alone doesn't prove a good reading, so check the driver status too.
 */

#ifndef IMU_CONVERSION_H
#define IMU_CONVERSION_H

#include <stdint.h>

// The chip's three directions.
enum class SensorAxis : uint8_t {
    X,
    Y,
    Z
};

// One reading in the chip's directions.
struct SensorVector {
    float x;
    float y;
    float z;
};

// One reading in the car's directions (see ImuTypes.h).
struct VehicleVector {
    float longitudinal;  // forward for accel, roll rate for gyro
    float lateral;       // left for accel, pitch rate for gyro
    float vertical;      // up for accel, yaw rate for gyro
};

// One row of the mounting table: which chip axis to use, and whether to flip it.
// Example: {SensorAxis::Y, -1} means "use chip Y, flipped".
struct AxisMapEntry {
    SensorAxis source;
    int8_t sign;  // +1 keeps it, -1 flips it
};

// The full mounting table: one row per car direction.
struct AxisMapping {
    AxisMapEntry longitudinal;
    AxisMapEntry lateral;
    AxisMapEntry vertical;
};

/* TODO(#41, mounting): this table is a GUESS. Replace it with the real mounting,
 * then do the tilt and rotate checks from issue #41.
 * NOTE: a wrong table still gives believable numbers, just on the wrong axis or
 * with the wrong sign. Only physically moving the board will catch that.
 */
const AxisMapping PLACEHOLDER_IMU_MOUNTING = {
    {SensorAxis::X, +1},  // longitudinal (+ forward) <- sensor +X  (UNCONFIRMED)
    {SensorAxis::Y, +1},  // lateral      (+ left)    <- sensor +Y  (UNCONFIRMED)
    {SensorAxis::Z, +1},  // vertical     (+ up)      <- sensor +Z  (UNCONFIRMED)
};

// Converts milli-g to g. Example: mgToG(-250) gives -0.25.
float mgToG(float milli_g);

/* Reorders and flips a chip reading into car directions.
 * Use the same table for accel and gyro.
 * NOTE: that only works for a real rotation, so check the table with
 * isValidAxisMapping(). A mirrored table would flip the gyro's sign.
 * NOTE: flipping 0 gives -0.0. It still equals 0, but printf may show "-0.000",
 * so don't mistake that for a bug in #44.
 */
VehicleVector mapSensorToVehicle(const SensorVector& sensor, const AxisMapping& mapping);

/* True if the table could be a real mounting: signs are +1 or -1, each chip axis
 * is used once, and it isn't a mirror image. Catches typos in the table.
 */
bool isValidAxisMapping(const AxisMapping& mapping);

#endif // IMU_CONVERSION_H
