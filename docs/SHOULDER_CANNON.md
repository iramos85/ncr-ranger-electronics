# NCR Ranger — Shoulder Cannon

## Confirmed Servo Wiring

| ESP32 GPIO | Physical Location | Axis | Movement |
|------------|-------------------|------|----------|
| GPIO18 | Upper servo, top of cradle | TILT | Up/down (YES) |
| GPIO19 | Lower servo, inside cradle | PAN | Left/right (NO) |

### Firmware Pin Definitions

```cpp
constexpr int TILT_PIN = 18; // Upper servo
constexpr int PAN_PIN  = 19; // Lower servo
```

### Important Notes

- GPIO18 = UPPER = TILT = YES
- GPIO19 = LOWER = PAN = NO
- Physical wiring and mechanical motion confirmed.
- Earlier documentation reversed PAN/TILT; this is the corrected mapping.
- Verify servo direction and mechanical limits before enabling automatic movement.

## Power System

- Controller: ESP32-WROOM
- Servos: 2x DS3218 20 kg
- Servo supply: 3S LiPo through Hobbywing 10A UBEC (~6 V)
- ESP32 supply: MP1584 buck converter regulated to 5 V
- Controller and servo grounds must be common.

## Pending Development

- Confirm safe pan and tilt angle limits
- Calibrate servo centers and movement directions
- Integrate ESP-NOW channel 6
- Add manual movement controls
- Implement scan and tracking behavior
- Connect plasma LEDs and audio effects
- Validate motion clearances while wearing armor

## PAN Servo Calibration — Initial Bench Test

- Controller: ESP32-WROOM
- Servo: Lower DS3218
- GPIO: 19
- Nominal center: 1500 microseconds
- Current software range: 1400–1600 microseconds
- Manual command step: 20 microseconds
- Direction: Left/right (PAN)
- Horn installed on rotating cradle
- Movement confirmed in both directions
- Upper TILT servo disconnected during testing
- Cannon head not installed

### Calibration Notes

20-microsecond commands produce visible movement.

The current 1400–1600 microsecond range is a temporary
software test range, NOT the final mechanical travel limit.

Safe mechanical endpoints still need to be established.

## Next Session — Safe PAN Limit Verification

**Status:** Procedure prepared; final travel limits are NOT yet hardware-verified.

### Preserve Known-Good Baseline

- Known-good firmware commit: `983fe1a` ("Calibrate lower PAN servo with 20us manual control").
- Firmware source: `firmware/shoulder-cannon/src/main.cpp`.
- Do not change the current PAN firmware or widen its `1400–1600 us` guardrails until clearance is physically verified.
- GPIO19 = lower PAN; GPIO18 = upper TILT. Keep TILT disconnected for PAN-only tests.
- Treat `DISARM` as detaching PWM, **not** as removing servo power or holding position. Use the physical power disconnect for an emergency stop.

### Safe PAN Test Procedure

1. Remove the cannon head and keep loose wiring, clothing and fingers clear of the cradle. Test off-body, with the pauldron supported in its intended orientation.
2. Power down before checking the servo horn screw, cradle fasteners and wire strain relief. Verify a common controller/UBEC ground.
3. With the upper TILT servo disconnected, open Serial at **115200 baud** and send `STATUS`. Confirm no servo movement occurs on boot.
4. Clear the mechanism, then send `ARM PAN`. Watch for any abrupt movement as the servo receives its 1500 us center command. If anything binds or jumps, disconnect power and correct the alignment.
5. Starting at center, send `PAN +` **one command at a time**, observing each 20 us movement; stop immediately for a hard stop, wire tension, horn interference, mounting flex, abnormal noise or increasing load.
6. Return toward center using `PAN -`, then repeat in the negative direction with separate `PAN -` commands. Do not exceed the existing 1400–1600 us software limits.
7. Record the last *comfortable* pulse value in each direction, plus where clearance first becomes questionable. Keep a safety margin inside both confirmed comfortable endpoints.
8. Send `DISARM` before turning off servo power. Do not rely on a detached servo to support the assembly mechanically.

### PAN Results — Fill After Hardware Test

| Measurement | Value | Status |
|-------------|-------|--------|
| Center | 1500 us | Nominal / currently tested |
| Positive software test bound | 1600 us | Temporary limit; final clearance unverified |
| Negative software test bound | 1400 us | Temporary limit; final clearance unverified |
| Last comfortable positive pulse | TBD | Not measured |
| Last comfortable negative pulse | TBD | Not measured |
| Proposed final operational limits | TBD | Requires margin and re-test |
| Mount/cable interference notes | TBD | Inspect during testing |
| Clearance with cannon head reinstalled | TBD | Must be checked separately |
| Clearance while wearing pauldron | TBD | Must be checked separately |

### TILT Calibration Gate

- Begin only after PAN is centered and the assembly is mechanically stable.
- Power **off** before reconnecting the upper TILT servo on GPIO18.
- With the horn/arm unloaded or disengaged as appropriate, establish the upper servo's neutral mechanically; do not assume its existing horn orientation is safe at 1500 us.
- Create a separate, manual-only TILT calibration stage with explicit arming, small increments, conservative initial software bounds and no automatic sweep.
- Confirm up/down direction, wire clearance, mechanical stops, and interference throughout the intended PAN range **before** allowing dual-axis motion.
- No scan/tracking, ESP-NOW movement commands, or plasma firing choreography until both axes have verified software limits and reliable stop behavior.

### Mechanical Refinement Checklist

- Verify four-bolt cradle seating and clearance around the pauldron lip/strap mount.
- Inspect printed layers and servo-ear towers for bending/cracks under load.
- Confirm horn fastener retention and servo shaft alignment.
- Route cables with strain relief and enough slack for both axes; keep them out of gears and pivot paths.
- Repeat final clearance tests **with cannon head attached** and while wearing the armor before enabling autonomous motion.

## PAN Calibration — Bench Update (2026-11-09)

User-reported tested values for current bench configuration:

```cpp
constexpr int PAN_MIN_US = 1200;
constexpr int PAN_MAX_US = 1800;
constexpr int PAN_CENTER_US = 1560;
```

- PAN = GPIO19, lower servo; TILT = GPIO18, upper servo.
- These values supersede the earlier 1400–1600 us provisional range for the bench-calibration plan.
- The center is mechanically selected at 1560 us, not the nominal 1500 us.
- The smooth position-command sketch discussed in chat uses 5 us steps at 40 ms intervals with `PAN <microseconds>`, `PAN CENTER`, `STOP`, and `DISARM`. **Its installation/build/hardware testing are not yet confirmed**; do not assume the GitHub firmware implements those commands.
- Test values are not yet final on-body operational limits; recheck with the cannon head mounted, wiring secured, and full PAN/TILT clearance, then leave appropriate margins.
- Existing repository firmware is intentionally unchanged; preserve known-good commit `983fe1a` until locally tested code is ready to commit.
