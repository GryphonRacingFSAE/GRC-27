/* The real ICM-20948 code. See ImuIcm20948.h.
 *
 * NOTE: library v1.3.2 was checked for hidden waits. begin() has short fixed
 * delays and capped retries, and dataReady() and getAGMT() don't wait. Re-check
 * if the library version changes.
 */

#include <Imu/ImuIcm20948.h>
#include <Imu/ImuQuality.h>
#include <pindefs.h>

#include <Arduino.h>
#include <SPI.h>
#include <ICM_20948.h>

static ICM_20948_SPI icm;
static bool icmReady = false;

// Applies our settings. Stops and returns false if any step fails.
static bool configureSensor() {
    const uint8_t accelAndGyro = ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr;

    // Measure all the time, not the power-saving mode.
    if (icm.setSampleMode(accelAndGyro, ICM_20948_Sample_Mode_Continuous) != ICM_20948_Stat_Ok) {
        return false;
    }

    // +/-8 g and +/-500 deg/s.
    ICM_20948_fss_t range;
    range.a = gpm8;
    range.g = dps500;
    if (icm.setFullScale(accelAndGyro, range) != ICM_20948_Stat_Ok) {
        return false;
    }

    // Filter out vibration above about 24 Hz. It must be on, because the chip
    // ignores the sample rate below without it.
    // NOTE: 24 Hz is my choice, not agreed with the team yet. It must stay below
    // 50 Hz, half the sample rate.
    ICM_20948_dlpcfg_t filter;
    filter.a = acc_d23bw9_n34bw4;
    filter.g = gyr_d23bw9_n35bw9;
    if (icm.setDLPFcfg(accelAndGyro, filter) != ICM_20948_Stat_Ok) {
        return false;
    }
    if (icm.enableDLPF(ICM_20948_Internal_Acc, true) != ICM_20948_Stat_Ok) {
        return false;
    }
    if (icm.enableDLPF(ICM_20948_Internal_Gyr, true) != ICM_20948_Stat_Ok) {
        return false;
    }

    // Sample rate = base rate / (1 + divider).
    // Accel: 1125 / 11 = about 102.3 Hz. Gyro: 1100 / 11 = 100 Hz.
    ICM_20948_smplrt_t rate;
    rate.a = 10;
    rate.g = 10;
    if (icm.setSampleRate(accelAndGyro, rate) != ICM_20948_Stat_Ok) {
        return false;
    }
    return true;
}

bool imuIcmInit() {
    icmReady = false;

    // Safe to call again on a retry.
    SPI.begin(IMU_SPI_SCK_PIN, IMU_SPI_MISO_PIN, IMU_SPI_MOSI_PIN, IMU_SPI_CS_PIN);

    if (icm.begin(IMU_SPI_CS_PIN, SPI, IMU_SPI_FREQ_HZ) != ICM_20948_Stat_Ok) {
        // begin() also starts the magnetometer, which we don't use. If only that
        // failed, the chip still passes its ID check, so wake it up and carry on.
        // NOTE: a broken magnetometer isn't reported. Revisit if one is needed.
        if (icm.checkID() != ICM_20948_Stat_Ok) {
            return false;
        }
        if (icm.sleep(false) != ICM_20948_Stat_Ok || icm.lowPower(false) != ICM_20948_Stat_Ok) {
            return false;
        }
    }

    if (!configureSensor()) {
        return false;
    }
    icmReady = true;
    return true;
}

bool imuIcmPoll(RawImuSample& out) {
    if (!icmReady) {
        return false;
    }

    // dataReady() is false for "nothing new" and for a failed read. Only the
    // second counts as a failure.
    if (!icm.dataReady()) {
        if (icm.status == ICM_20948_Stat_NoData) {
            return false;
        }
        out = {false, false, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false, false};
        return true;
    }

    icm.getAGMT();
    if (icm.status != ICM_20948_Stat_Ok) {
        out = {false, false, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, false, false};
        return true;
    }

    // accX() gives mg and gyrX() gives deg/s. The raw counts are only used for
    // the saturation check.
    out.accelRead = true;
    out.gyroRead = true;
    out.accel_mg = {icm.accX(), icm.accY(), icm.accZ()};
    out.gyro_dps = {icm.gyrX(), icm.gyrY(), icm.gyrZ()};
    out.accelSaturated = isRawSaturated(icm.agmt.acc.axes.x, icm.agmt.acc.axes.y, icm.agmt.acc.axes.z);
    out.gyroSaturated = isRawSaturated(icm.agmt.gyr.axes.x, icm.agmt.gyr.axes.y, icm.agmt.gyr.axes.z);

    // NOTE: an unplugged sensor usually shows up as dataReady() never being true,
    // not as an error. The data then just goes stale.
    return true;
}
