# NCR Ranger Electronics

Firmware and documentation for the NCR Ranger costume electronics system.

## Hardware

### Pip-Boy Controller
- Arduino UNO R4 WiFi
- 480x320 ILI9486 touchscreen shield

### Radio Bridge
- Seeed XIAO ESP32-S3

### Helmet Node
- Adafruit Feather ESP32 V2
- BNO085 IMU
- Helmet fan
- Lens lighting

### Shoulder Cannon
- ESP32 controller
- BNO085 IMU
- Dual servo positioning
- Lighting

### Audio
- Teensy 4.1
- Teensy Audio Shield Rev D
- Voice processing
- Sound effects

## Architecture

Pip-Boy / UNO R4
→ XIAO ESP32-S3 radio bridge
→ ESP-NOW
→ costume subsystem nodes

## Pip-Boy Baseline

Known-good local UI.

Working:
- TFT display
- Resistive touchscreen
- Tracking button
- Cannon light button
- SFX test button
- Serial diagnostic events

Not yet integrated:
- XIAO radio bridge
- Helmet online status
- Cannon online status
- Audio online status

## Serial

Baud rate: 115200
