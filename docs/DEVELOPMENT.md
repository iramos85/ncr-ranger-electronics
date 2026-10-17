# NCR Ranger Electronics — Development Standards

## Source of Truth
GitHub is the canonical source for all firmware.

Never leave the only copy of working firmware on a single computer.

## Firmware Structure
Each controller has its own directory under firmware/.

Each PlatformIO project contains:
- platformio.ini
- src/main.cpp
- include/ (when needed)
- lib/ (when needed)

## Development Workflow

### Before Working
1. Check git status.
2. Fetch remote changes.
3. Pull the current branch with --ff-only.
4. Confirm the intended firmware and hardware target.

### After Working
1. Compile the changed firmware.
2. Test on hardware when available.
3. Review git diff.
4. Commit source and documentation changes.
5. Push the current feature branch.

## Branches
- main: stable, verified firmware
- feature/*: new development
- fix/*: bug fixes

Incomplete work may be committed to feature branches.
Never merge untested hardware changes into main.

## Hardware Safety
- Preserve a known-good firmware version before flashing.
- Record board models and pin assignments.
- Never reassign pins without checking existing wiring.
- Never assume a command was executed just because it was transmitted.

## Communication
- Metro ESP32-S3 is the master controller.
- ESP-NOW channel: 6.
- Helmet, cannon, and audio nodes use separate command namespaces.
- Nodes acknowledge commands and report their status.
- ESP-NOW callbacks must not perform display updates or blocking work.

## Audio
- Audio ESP32 communicates with Teensy over UART.
- Audio node source must be recovered before replacing firmware.
- SD card playback and voice processing must be tested independently.
- Music, effects, and microphone audio should have separate gain controls.

## Multi-Computer Development
Always push your feature branch before changing computers.

Always fetch and fast-forward before resuming work on another computer.

Never force-push shared development branches without explicitly reviewing the consequences.
