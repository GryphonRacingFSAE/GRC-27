/* Decides whether a reading can be used, and whether it is recent enough.
 *
 * Functions take the time as an input, so tests can pretend any time they like.
 * Zero and negative readings are real measurements and are accepted.
 */

#ifndef IMU_QUALITY_H
#define IMU_QUALITY_H

#include <Imu/ImuTypes.h>
#include <stdint.h>

// True if all three are real numbers (not NaN or infinity).
bool isFiniteReading(float a, float b, float c);

/* True if any raw sensor count hit its limit (32767 or -32768), meaning the real
 * value may be bigger.
 * NOTE: assumes plain signed 16-bit counts. Another sensor may differ.
 */
bool isRawSaturated(int16_t a, int16_t b, int16_t c);

/* True if the timestamp is no older than limit_ms. Works across the millis() wrap.
 * NOTE: read now_ms AFTER copying the snapshot. A timestamp slightly later than
 * now_ms looks hugely old and counts as stale.
 */
bool isAgeWithinLimit(uint32_t timestamp_ms, uint32_t now_ms, uint32_t limit_ms);

// True if the reading is valid and recent. #43 should check this before sending.
bool isFresh(const ImuAccel& reading, uint32_t now_ms);
bool isFresh(const ImuGyro& reading, uint32_t now_ms);

/* Stores a new reading, already in car directions and final units.
 * If any value is NaN or infinity, the old values are kept, the reading is marked
 * InvalidValue and this returns false.
 * now_ms must be when the sample was READ, not when the task woke up.
 */
bool acceptReading(ImuAccel& reading, float longitudinal_g, float lateral_g, float vertical_g, uint32_t now_ms);
bool acceptReading(ImuGyro& reading, float rollRate_dps, float pitchRate_dps, float yawRate_dps, uint32_t now_ms);

/* Marks the latest attempt as failed. The old values and their time are kept, so
 * diagnostics can still show "last good value, N ms ago".
 * NOTE: passing Ok or NotStarted is stored as ReadFailed.
 */
void markFailed(ImuAccel& reading, ImuStatus reason);
void markFailed(ImuGyro& reading, ImuStatus reason);

#endif // IMU_QUALITY_H
