/* A pretend IMU that plays back a fixed script, so CAN and serial can be tested
 * without a sensor. Each poll plays one line, and one line is one 10 ms tick.
 * The script repeats forever.
 *
 * NOTE: never use this on the car. ImuTask.cpp only picks it when the firmware
 * is built with -DIMU_USE_FAKE_SOURCE.
 */

#ifndef IMU_FAKE_SOURCE_H
#define IMU_FAKE_SOURCE_H

#include <Imu/ImuPipeline.h>
#include <stdint.h>

// One line of the script. hasData = false means nothing new this tick.
struct FakeImuEntry {
    bool hasData;
    RawImuSample raw;
};

// Which script is playing, and where it is up to.
struct FakeImuSource {
    const FakeImuEntry* script;
    uint32_t length;  // number of lines
    uint32_t index;   // next line to play
};

// The built-in script. Each line is described in ImuFakeSource.cpp.
extern const FakeImuEntry DEFAULT_FAKE_IMU_SCRIPT[];
extern const uint32_t DEFAULT_FAKE_IMU_SCRIPT_LENGTH;

// Picks a script and starts from its first line.
void initFakeImuSource(FakeImuSource& source, const FakeImuEntry* script, uint32_t length);

// Plays the next line. Returns true if "out" holds a read attempt, or false if
// there is nothing new.
bool pollFakeImu(FakeImuSource& source, RawImuSample& out);

#endif // IMU_FAKE_SOURCE_H
