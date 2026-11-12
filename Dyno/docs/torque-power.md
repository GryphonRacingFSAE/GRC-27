# Torque and power (#37)

## What it does

Turns **calibrated force** (from the load cell, after #35's calibration) and **shaft speed** (engine RPM from the ECU, #33) into torque and power. It is pure maths plus validity checks, in `lib/DynoCore/src/TorquePower.{h,cpp}`. It has no hardware, serial or RTOS code, so it is tested entirely on the host.

Every result says whether it is **valid** and, if not, **why**. A missing value is never shown as `0`: invalid values are `NaN` (blank in the CSV), and a real zero stays a valid `0`.

## Formulas and units

```text
torque_Nm             = force_N × effective_arm_m
shaft_speed_rad_per_s = shaft_rpm × 2 × pi / 60
power_W               = torque_Nm × shaft_speed_rad_per_s
power_kW              = power_W / 1000
mechanical_hp         = power_W / 745.699872
```

- **Units are in every name** (`force_n`, `effective_arm_m`, `torque_nm`, `power_w`, `horsepower_hp`). The arm is in **metres**. `metresFromMillimetres()` converts a drawing's millimetres: 250 mm → 0.25 m.
- **Horsepower is mechanical hp** (745.699872 W, [NIST SP 811 B.9](https://www.nist.gov/pml/special-publication-811/nist-guide-si-appendix-b-conversion-factors/nist-guide-si-appendix-b9)), not metric hp/PS (735.5 W).
- **Never use `torque × rpm / 5252`.** That shortcut expects pound-feet and gives nonsense with N·m. The old GRC26 firmware did this.

Worked example (issue #37, **fixture geometry**): 100 N on a perpendicular 0.25 m arm at 600 rpm on the same shaft:

| Step | Value |
|---|---|
| Torque | 100 × 0.25 = **25 N·m** |
| Speed | 600 × 2π / 60 = 62.831853 rad/s |
| Power | 25 × 62.831853 = **1570.796 W** = 1.570796 kW |
| Horsepower | 1570.796 / 745.699872 = **2.106473 hp** |

At 6000 rpm: 628.318531 rad/s, 15707.963 W, 21.064726 hp.

## Inputs

| Input | Type | Where it comes from |
|---|---|---|
| Force | `ForceMeasurement` (`ForceMeasurement.h`) | **#35** fills it from a `LoadCellSample` and the accepted calibration. Until then the firmware has no force, so torque and power read `ForceUnavailable`. |
| Shaft speed | `ShaftSpeedSample` | `engineSpeedFromEcu(ecu)`: `EngineSpeed` from 0x520 and the time that frame arrived. |
| Geometry | `MechanicalSetup` | `dynoMechanicalSetup()` (`include/Defines/DynoCalcSetup.h`) from `DynoConfig.h`: `EFFECTIVE_ARM_M`, `GEOMETRY_CONFIRMED`, `SAME_SHAFT_CONFIRMED`. |
| Limits | `CalcLimits` | `dynoCalcLimits()` from `DynoConfig.h`: `FORCE_FRESHNESS_MS`, `CAN_FRESHNESS_MS`, `MAX_SOURCE_SKEW_MS`, `EFFECTIVE_ARM_MAX_M`. |

`ForceMeasurement` is the contract with #35:

| Field | Meaning |
|---|---|
| `available` | `false` = no usable force: no sample yet, load-cell fault, or no accepted calibration. A default-constructed one is unavailable, never a valid 0 N. |
| `force_n` | Newtons, signed. The sign comes from the calibration (see "Sign convention"). |
| `timestamp_ms` | `millis()` of the raw load-cell sample the force came from. |
| `calibration_ready` | `false` while a calibration or tare is collecting. The force is then unusable. |
| `calibration_version`, `tare_version` | The pair (#35) this force was computed under. These are 32-bit counters that wrap after about 4 billion changes; only meaningful together with `calibration_ready`. |

**Units are enforced by naming**: every field and function carries its unit, and nothing converts silently. The only automatic "wrong unit" catch is the arm's 2.0 m typo guard (`ArmInvalid`), because a millimetre value in the metres field is the likely mistake.

Take `now_ms` (`millis()`) **after** copying the inputs out of their mailboxes. Otherwise a fresh input can look ~49 days old and read as stale.

## Result contract (for #38)

`computeTorquePower(force, speed, setup, limits, now_ms)` returns a `TorquePowerResult`:

| Field | CSV column | Meaning |
|---|---|---|
| `torque.valid`, `torque.reason` | — | Torque is usable / why not |
| `torque.torque_nm` | `torque_nm` | N·m, signed. `NaN` (blank) when invalid. |
| `power.valid`, `power.reason` | — | Power is usable / why not |
| `power.power_w` | `power_w` | W, signed. `NaN` (blank) when invalid. |
| `power.power_kw` | — | kW (not in the CSV schema; available for diagnostics) |
| `power.horsepower_hp` | `horsepower_hp` | Mechanical hp. `NaN` (blank) when invalid. |
| `reverse_torque` | — | Valid torque below zero (see "Sign convention"). |
| `force_timestamp_ms`, `rpm_timestamp_ms` | — | The inputs' own timestamps, for checking source ages. |
| `versions_known`, `calibration_version`, `tare_version` | `calibration_version`, `tare_version` | The pair of **the force this result came from**. Blank when `versions_known` is false. |

`calcReasonName(reason)` gives a printable name, e.g. `"ForceStale"`.

## When a result is invalid

The checks run in this order, and the **first failing one** is the reason. If torque is invalid, power is invalid with **the same reason** (the root cause).

| # | Reason | Affects | Condition |
|---|---|---|---|
| 1 | `ForceUnavailable` | torque + power | `force.available` is false |
| 2 | `CalibrationNotReady` | torque + power | `force.calibration_ready` is false (calibration/tare collecting) |
| 3 | `ForceNotFinite` | torque + power | force is NaN or ±infinity |
| 4 | `ForceStale` | torque + power | force age ≥ `FORCE_FRESHNESS_MS` |
| 5 | `GeometryUnconfirmed` | torque + power | `GEOMETRY_CONFIRMED` is false |
| 6 | `ArmInvalid` | torque + power | arm ≤ 0, NaN, infinite, or > `EFFECTIVE_ARM_MAX_M` |
| 7 | `ShaftSpeedUnconfirmed` | power only | `SAME_SHAFT_CONFIRMED` is false |
| 8 | `RpmMissing` | power only | 0x520 never received |
| 9 | `RpmInvalid` | power only | RPM is NaN, infinite or negative |
| 10 | `RpmStale` | power only | RPM age ≥ `CAN_FRESHNESS_MS` |
| 11 | `SourceSkewTooLarge` | power only | force and RPM timestamps more than `MAX_SOURCE_SKEW_MS` apart |

So **torque can be valid without RPM; power cannot.** A valid 0 rpm gives valid torque and valid 0 W.

**To agree with #35:** if #35 marks force both `available = false` and `calibration_ready = false` while collecting, the reason reads `ForceUnavailable` (check 1 wins). The output is invalid either way. If the operator should see `CalibrationNotReady` during collection, #35 keeps `available = true` with `calibration_ready = false`, or the order of checks 1 and 2 is swapped then.

## Freshness and source skew

"Fresh" means **age < limit** (an age equal to the limit is stale), the same rule as `EcuState::isFresh`. Ages and gaps use unsigned subtraction, so they stay correct when `millis()` wraps every ~49.7 days. A timestamp newer than `now` counts as stale.

| Setting | Value | Why |
|---|---|---|
| `FORCE_FRESHNESS_MS` | 500 ms | The HX711 gives a sample every 100 ms (10 Hz) or 12.5 ms (80 Hz). After ~450 ms without one, #34 declares a Fault, so a 500 ms-old force means the load cell has stopped. |
| `CAN_FRESHNESS_MS` | 2000 ms | #33's existing ECU freshness rule, reused for RPM. |
| `MAX_SOURCE_SKEW_MS` | 250 ms | Power multiplies the **latest** force by the **latest** RPM. Those were not measured at the same moment. With both sources healthy (assumed ≥ 10 Hz each), their timestamps are within ~100 ms of each other. 250 ms leaves margin, but stops an RPM that is 2 s old (still "fresh" by CAN's rule) being paired with a force from now. The limit is inclusive: exactly 250 ms apart is accepted. |
| `EFFECTIVE_ARM_MAX_M` | 2.0 m | A typo guard, not a measurement. Real dyno arms are well under 2 m; a millimetre value typed into the metres field (e.g. `250`) is rejected instead of giving a 1000× torque. |

**The two timestamps are different kinds of time:** the force's is when the HX711 sample was **read** by the load-cell task, while the RPM's is when the ECU's CAN frame **arrived** (the ECU measured the speed a little before sending it, by an unknown amount). So the skew limit bounds how far apart the dyno *received* the two values, not exactly when each was physically measured. Keep that margin in mind when retuning it.

All are first guesses to retune on hardware once real rates and noise are known. If you change one, rework the boundary rows in `TorquePowerExamples.h`: `test_config` fails until they match.

## Sign convention and negative force

- The sign comes **only from the calibration (#35)**: positive force is the direction the reference load was applied. There is no extra sign-flip setting, so a direction mistake isn't hidden.
- **Calibrate in the direction the engine pushes the load cell when it drives the dyno.** Then positive force = positive torque = engine producing power, which is the normal operating state.
- **Negative force is kept as signed negative torque and power**, never clamped to 0 or made positive (issue table: −100 N → −25 N·m, −1570.796 W). It sets `reverse_torque`, the flag for an **unsupported operating state**. It means one of:
  - the dyno is driving the engine (e.g. the engine is being motored or is on overrun), or
  - the load cell was calibrated in the opposite direction (re-calibrate in the engine's push direction).
- A **negative RPM** is not a supported state either (the ECU's RPM is unsigned), so it gives `RpmInvalid`.

## Calibration and tare changes

- The result's version pair is **copied only from the force it was computed from**, never from "current" settings. So a force computed under calibration 1 can never appear with calibration 2's label, even if calibration 2 was accepted in between.
- While calibration or tare is collecting, #35/#36 set `calibration_ready = 0`. Torque and power are then invalid (`CalibrationNotReady`) and the versions are blank.
- Versions are blank whenever the force itself is unusable (unavailable, not ready, not finite, stale). They stay known when only the geometry is the problem, because they still describe a good force.
- This module never recalibrates counts or applies tare. That is #35/#36's job, done once.

## Example dataset for #38

`lib/DynoCore/src/TorquePowerExamples.h` holds 15 rows of inputs with their hand-worked expected results:
- valid rows: reference, zero force, zero rpm, negative force, 6000 rpm
- torque-only rows: RPM missing, RPM stale, skew too large, shaft unconfirmed
- invalid rows: stale force, calibration collecting, force unavailable, NaN force, geometry unconfirmed, 250 m arm

`test_torque_power` checks every row, so #38 can use them directly as fixtures. **Their geometry is the issue's example (0.25 m, same shaft), labelled FIXTURE ONLY, not the real dyno.**

## Where the firmware uses it (until #38)

The temporary `MonitorTask` peeks the ECU mailbox, takes `millis()`, and calls `computeTorquePower()` with the real `DynoConfig.h` setup. It prints a line every 500 ms. #38 moves this call into its output task. Today it prints:

```
# calc torque_nm=-(ForceUnavailable) power_w=-(ForceUnavailable) reverse_torque=0 versions=- force_ts_ms=0 rpm_ts_ms=51230
```

`ForceUnavailable` is expected until #35 provides calibrated force. At startup (repeated every 10 s), these lines print until the setup is confirmed:

```
# WARNING: dyno geometry unconfirmed (DynoConfig.h): torque and power are disabled
# WARNING: ECU RPM not confirmed to be the load-cell shaft (DynoConfig.h): power is disabled
```

## Setup note: ask the mechanical team

The real values are **unknown**, so real torque and power stay invalid until this note is filled in. Do not substitute a convenient bracket length or a guessed ratio.

| Question | Answer |
|---|---|
| Which shaft does the load cell measure: the engine crank, or a roller/brake/absorber shaft? | ______ |
| Is there a belt, chain or gearbox between the engine crank and that shaft? | ______ |
| Force direction: which way does the engine push the load cell when it drives the dyno? | ______ |
| Mounting: how is the load cell attached (lever arm, link rod, angle)? Sketch or photo. | ______ |
| **Effective arm**: the **perpendicular** distance from the shaft's axis to the load cell's line of force, in **metres**. For an angled link, this is the perpendicular distance, not the bracket length. | ______ m |
| How was it measured, and by whom? | ______ |
| Positive-force convention: calibrate with the reference load in the direction above? | ______ |

`GearPosn` from the ECU is a raw gear code, **not** a ratio. Don't use it to convert speeds, and don't add drivetrain-efficiency or engine-power corrections.

## Checking that RPM and torque are on the same shaft

Power is only physical if the RPM describes **the shaft the load cell measures**. Before setting `SAME_SHAFT_CONFIRMED`:

1. From the setup note: if the load cell measures the **engine crank** (absorber directly coupled, no belt/gear), ECU engine RPM is the right speed.
2. Check it on hardware: measure the load-cell shaft's speed independently (optical/strobe tachometer on that shaft) at two or three steady speeds, and compare with the ECU RPM in the `# ecu` line. They should agree within the tachometer's accuracy. Record: ______
3. If the load cell measures a **different shaft** (roller, after a belt/gearbox), ECU RPM must **not** be used. Leave `SAME_SHAFT_CONFIRMED = false`, so power stays invalid. Supporting another shaft (a measured ratio or a separate speed sensor) is a design change for the team, not a setting to guess.

## When the answers arrive

1. Fill in the setup note above.
2. In `include/Defines/DynoConfig.h`: set `EFFECTIVE_ARM_M` (metres), then `GEOMETRY_CONFIRMED = true`. A `static_assert` stops the build if the arm is still 0 or above 2 m.
3. Only after the same-shaft check passes: set `SAME_SHAFT_CONFIRMED = true`.
4. Rebuild. No other code changes are needed. `test_config` checks that torque/power follow these flags.

## Tests

```
pio test -e native -f test_torque_power   # maths, validity, timing, versions, ECU adapter, #38 dataset
pio test -e native -f test_config         # the real DynoConfig.h setup can't produce output until confirmed
```

Expected values are typed in from the issue or worked out by hand, never recomputed the way the code does it. Floating-point comparisons use a relative tolerance of 1e-6. Each important check was confirmed to fail by temporarily breaking the code (e.g. `÷5252`, dropping `2π`, `<` → `<=` on freshness, removing the skew check, copying the wrong version, clamping negative force).

## Hardware checklist (carried into #39)

Simulation does not validate the mechanical model. These are **blockers to physical validation**, not passes:

1. **Setup note** filled in and signed off by the mechanical team. ______
2. **Same-shaft check** (above) done and recorded. ______
3. **Static torque check** (needs #35 calibration): with the engine off and the shaft locked, hang a known mass on the arm at the measured distance. Torque should be `m × 9.81 × arm`, within the calibration tolerance. Also check the sign is positive when the load is applied in the engine's push direction. Expected ______ N·m, measured ______ N·m.
4. **Running check:** at a steady speed, check that the `# calc` line shows plausible torque and power, with `reverse_torque=0`, and that it goes invalid (`RpmStale`/`ForceStale`) within the limits when CAN or the load cell is unplugged.
