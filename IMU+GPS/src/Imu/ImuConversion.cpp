// Unit conversion and mounting-table code. See ImuConversion.h.

#include <Imu/ImuConversion.h>

#include <math.h>

// NaN stays NaN, so bad data stays marked as bad.
float mgToG(float milli_g) {
    return milli_g / 1000.0f;
}

// Picks one value out of a chip reading.
static float readSensorAxis(const SensorVector& sensor, SensorAxis axis) {
    switch (axis) {
        case SensorAxis::X: return sensor.x;
        case SensorAxis::Y: return sensor.y;
        case SensorAxis::Z: return sensor.z;
    }
    // Only reached if "axis" is corrupted. NaN makes the mistake obvious,
    // where a 0 would look believable.
    return NAN;
}

// Applies one table row: read the chip axis, then flip it if needed.
static float applyEntry(const SensorVector& sensor, const AxisMapEntry& entry) {
    return readSensorAxis(sensor, entry.source) * entry.sign;
}

VehicleVector mapSensorToVehicle(const SensorVector& sensor, const AxisMapping& mapping) {
    VehicleVector vehicle;

    vehicle.longitudinal = applyEntry(sensor, mapping.longitudinal);
    vehicle.lateral = applyEntry(sensor, mapping.lateral);
    vehicle.vertical = applyEntry(sensor, mapping.vertical);

    return vehicle;
}

// Turns one table row into three numbers for the mirror check below.
// Example: {Y, -1} becomes (0, -1, 0).
static void entryToRow(const AxisMapEntry& entry, int row[3]) {
    row[0] = 0;
    row[1] = 0;
    row[2] = 0;

    row[static_cast<int>(entry.source)] = entry.sign;
}

bool isValidAxisMapping(const AxisMapping& mapping) {
    const AxisMapEntry entries[3] = {mapping.longitudinal, mapping.lateral, mapping.vertical};

    // Check 1: every sign is +1 or -1, and every source is a real chip axis.
    for (int i = 0; i < 3; i++) {
        if (entries[i].sign != 1 && entries[i].sign != -1) {
            return false;
        }
        if (static_cast<int>(entries[i].source) > 2) {
            return false;
        }
    }

    // Check 2: each chip axis is used once. Otherwise one axis is being ignored.
    if (entries[0].source == entries[1].source ||
        entries[0].source == entries[2].source ||
        entries[1].source == entries[2].source) {
        return false;
    }

    // Check 3: the table isn't a mirror image. A real chip can be turned any way,
    // but never into its own reflection, so a mirrored table means a sign is
    // wrong. The determinant of the table is +1 for a rotation, -1 for a mirror.
    int m[3][3];
    entryToRow(entries[0], m[0]);
    entryToRow(entries[1], m[1]);
    entryToRow(entries[2], m[2]);

    int determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                    - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                    + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    return determinant == 1;
}
