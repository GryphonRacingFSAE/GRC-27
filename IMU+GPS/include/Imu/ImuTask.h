/* The FreeRTOS IMU task, and how other tasks get its data.
 *
 * Data is shared as one locked "latest value", not a queue. CAN and serial only
 * want the newest reading, and with no queue there is nothing to fill up, so a
 * slow reader can't hold up the IMU.
 *
 * The real sensor is the default. The fake is only used in firmware built with
 * -DIMU_USE_FAKE_SOURCE, which shows a compiler warning.
 *
 * TODO(#40): Riyan to confirm the priority, stack size and latest-value approach.
 */

#ifndef IMU_TASK_H
#define IMU_TASK_H

#include <Imu/ImuDiagnostics.h>

// Creates the lock and the IMU task. Returns false if FreeRTOS couldn't create
// them. The sensor starts inside the task, so a missing IMU can't delay this.
bool imuTaskStart();

/* Copies the latest IMU record into "out". Returns false if the task hasn't been
 * started or the lock stayed busy; just try again next time.
 * Then use isFresh(out.latest.accel, millis()) before trusting the values.
 * NOTE: read millis() AFTER this call, not before.
 */
bool imuGetLatest(ImuDiagnostics& out);

#endif // IMU_TASK_H
