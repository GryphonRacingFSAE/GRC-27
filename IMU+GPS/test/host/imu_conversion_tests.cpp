/* Laptop tests for ImuConversion. Prints PASS or FAIL for each check.
 *
 * Build and run from the IMU+GPS folder in WSL:
 *   g++ -std=c++11 -Wall -Wextra -Iinclude test/host/imu_conversion_tests.cpp src/Imu/ImuConversion.cpp -o /tmp/imu_conversion_tests
 *   /tmp/imu_conversion_tests
 *
 * NOTE: this is in test/host/, not test/test_*, so `pio test` doesn't try to
 * build it for the ESP32.
 * NOTE: when the real mounting is known, add checks for the real table here.
 */

#include <Imu/ImuConversion.h>
#include <math.h>
#include <stdio.h>

static int passCount = 0;
static int failCount = 0;

// Decimals aren't stored exactly, so "equal" means within this tiny amount.
static const float TOLERANCE = 0.000001f;

// Compares a number we got with the number we expected.
static void checkFloat(const char* name, float actual, float expected) {
    bool ok = fabsf(actual - expected) <= TOLERANCE;

    printf("%s  %-48s expected %8.3f  got %8.3f\n", ok ? "PASS" : "FAIL", name, expected, actual);

    if (ok) {
        passCount++;
    } else {
        failCount++;
    }
}

// For yes/no checks.
static void checkTrue(const char* name, bool condition) {
    printf("%s  %s\n", condition ? "PASS" : "FAIL", name);
    if (condition) {
        passCount++;
    } else {
        failCount++;
    }
}

// Test group 1: mg -> g conversion.
// The first three are the exact examples required by issue #41.
static void testMgToG() {
    printf("\n-- mg to g --\n");
    checkFloat("mgToG(1000) == 1.000", mgToG(1000.0f), 1.000f);
    checkFloat("mgToG(-250) == -0.250", mgToG(-250.0f), -0.250f);
    checkFloat("mgToG(0) == 0.000", mgToG(0.0f), 0.000f);

    // Uses isnan() because NaN == NaN is always false.
    checkTrue("mgToG(NaN) stays NaN (invalid never becomes a number)", isnan(mgToG(NAN)));
}

// Test group 2: the placeholder mounting table.
// It doesn't reorder or flip anything, so values come out as they went in.
static void testPlaceholderMapping() {
    printf("\n-- placeholder mounting (identity, UNCONFIRMED) --\n");

    // A different number on each axis, so a mix-up can't hide.
    SensorVector in = {1.0f, 2.0f, 3.0f};

    VehicleVector out = mapSensorToVehicle(in, PLACEHOLDER_IMU_MOUNTING);

    checkFloat("placeholder: longitudinal <- +X", out.longitudinal, 1.0f);
    checkFloat("placeholder: lateral <- +Y", out.lateral, 2.0f);
    checkFloat("placeholder: vertical <- +Z", out.vertical, 3.0f);
}

/* A test-only table that moves every axis and flips two signs, so a swapped
 * axis or a missed sign always changes the answer:
 *   longitudinal = -Z,  lateral = +X,  vertical = -Y
 * It is a real rotation (right-handed), not a guess at the real mounting.
 *
 * Why not just test with the placeholder? Because with an identity table,
 * some bugs (like reading the wrong axis in one spot) could still give the
 * right-looking answer. This table makes every mistake visible.
 */
static const AxisMapping TEST_MOUNTING = {
    {SensorAxis::Z, -1},  // longitudinal = -Z
    {SensorAxis::X, +1},  // lateral      = +X
    {SensorAxis::Y, -1},  // vertical     = -Y
};

// Test group 3: the test table, with three kinds of input.
static void testRotatedMapping() {
    // 3a. Each answer can only come from one input, so this shows which axis
    //     and sign was used.
    printf("\n-- test mounting: long=-Z, lat=+X, vert=-Y (accel-style input) --\n");
    SensorVector in = {1.0f, 2.0f, 3.0f};
    VehicleVector out = mapSensorToVehicle(in, TEST_MOUNTING);
    checkFloat("(1,2,3): longitudinal = -Z = -3", out.longitudinal, -3.0f);
    checkFloat("(1,2,3): lateral = +X = 1", out.lateral, 1.0f);
    checkFloat("(1,2,3): vertical = -Y = -2", out.vertical, -2.0f);

    // 3b. Gyro data with negative inputs. Example: yaw = -Y = -(-20) = +20.
    printf("\n-- test mounting (gyro-style input, mixed signs) --\n");
    SensorVector gyro = {10.0f, -20.0f, 30.0f};
    VehicleVector rates = mapSensorToVehicle(gyro, TEST_MOUNTING);
    checkFloat("(10,-20,30): roll rate = -Z = -30", rates.longitudinal, -30.0f);
    checkFloat("(10,-20,30): pitch rate = +X = 10", rates.lateral, 10.0f);
    checkFloat("(10,-20,30): yaw rate = -Y = 20", rates.vertical, 20.0f);

    // 3c. Zero must stay zero. Flipped axes may print "-0.000", which is harmless.
    printf("\n-- test mounting (all zero) --\n");
    SensorVector zero = {0.0f, 0.0f, 0.0f};
    VehicleVector zeroOut = mapSensorToVehicle(zero, TEST_MOUNTING);
    checkFloat("(0,0,0): longitudinal = 0", zeroOut.longitudinal, 0.0f);
    checkFloat("(0,0,0): lateral = 0", zeroOut.lateral, 0.0f);
    checkFloat("(0,0,0): vertical = 0", zeroOut.vertical, 0.0f);
}

// Test group 4: the table checker (isValidAxisMapping).
// Good tables must be accepted and broken ones rejected.
static void testMappingValidation() {
    printf("\n-- isValidAxisMapping --\n");

    // These two are real rotations, so both should be accepted.
    checkTrue("placeholder table is valid", isValidAxisMapping(PLACEHOLDER_IMU_MOUNTING));
    checkTrue("test table is valid", isValidAxisMapping(TEST_MOUNTING));

    // Broken: chip X is used for two car directions (and chip Y is ignored).
    const AxisMapping duplicateAxis = {{SensorAxis::X, +1}, {SensorAxis::X, +1}, {SensorAxis::Z, +1}};
    checkTrue("rejects sensor X used twice", !isValidAxisMapping(duplicateAxis));

    // Broken: a sign of 0 would wipe out that direction completely.
    const AxisMapping zeroSign = {{SensorAxis::X, 0}, {SensorAxis::Y, +1}, {SensorAxis::Z, +1}};
    checkTrue("rejects a sign of 0", !isValidAxisMapping(zeroSign));

    // Flipping only one axis makes a mirror image: impossible for a real chip.
    const AxisMapping mirrored = {{SensorAxis::X, +1}, {SensorAxis::Y, -1}, {SensorAxis::Z, +1}};
    checkTrue("rejects a mirrored (left-handed) table", !isValidAxisMapping(mirrored));
}

int main() {
    testMgToG();
    testPlaceholderMapping();
    testRotatedMapping();
    testMappingValidation();

    printf("\n%d passed, %d failed\n", passCount, failCount);

    // 0 = all passed, 1 = at least one check failed.
    return failCount == 0 ? 0 : 1;
}
