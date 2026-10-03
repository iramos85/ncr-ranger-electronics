# XIAO Radio Bridge

Seeed XIAO ESP32-S3 radio bridge for the NCR Ranger electronics system.

## Hardware

- Board: Seeed XIAO ESP32-S3
- I2C slave address: `0x42`
- SDA: D4 / GPIO5
- SCL: D5 / GPIO6
- I2C speed: 100 kHz
- ESP-NOW channel: 6
- ESP-NOW transport: broadcast
- Protocol frame size: 16 bytes

## Architecture

Arduino UNO R4 Pip-Boy
→ I2C / Wire1
→ XIAO ESP32-S3
→ ESP-NOW
→ Costume subsystem nodes

## Protocol

The XIAO acts as a generic bridge.

1. UNO writes a command over I2C.
2. XIAO immediately reports `WAIT`.
3. XIAO broadcasts the command over ESP-NOW.
4. A subsystem responds over ESP-NOW.
5. XIAO stores the latest response.
6. UNO reads that response over I2C.

Example:

PING
→ WAIT
→ ESP-NOW PING
→ PONG
→ UNO reads PONG

The bridge does not interpret subsystem commands. Commands such as
`FAN_ON`, `FAN_OFF`, `LENSES_ON`, and `LENSES_OFF` are forwarded unchanged.
