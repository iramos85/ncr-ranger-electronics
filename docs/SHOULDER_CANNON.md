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
