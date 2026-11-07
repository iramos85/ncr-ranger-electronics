# NCR Ranger Audio System

## Architecture

Metro ESP32-S3 Pip-Boy
    |
    | ESP-NOW / Channel 6
    v
Audio ESP32-WROOM
    |
    | UART / 115200 baud
    | ESP32 TX17 -> Teensy RX1
    | ESP32 RX16 <- Teensy TX1
    v
Teensy 4.1 + Audio Shield Rev D
    |
    v
MAX98306 Stereo Amplifier
    |
    v
Two 40mm / 4-ohm / 5W Speakers

All interconnected controllers share signal ground.

## Firmware

- firmware/metro-pipboy
- firmware/audio-esp32
- firmware/audio-teensy

Development branch:
feature/ranger-audio-fx-radio

## Radio New Vegas

43 WAV tracks stored on Teensy microSD.

Filename convention:
NV001.WAV through NV043.WAV

Playlist metadata:
firmware/audio-teensy/assets/playlist.csv

Commands:
MUSIC:PLAY
MUSIC:PAUSE
MUSIC:NEXT
MUSIC:PREV
MUSIC:STOP
MUSIC:STATUS
MUSIC:VOLUP
MUSIC:VOLDOWN
MUSIC:VOLUME

Music volume:
Default 50%, mixer gain 0.08
Maximum mixer gain 0.16

## Tactical FX

SFX_TEST maps to READY.WAV.

Planned and implemented FX commands
should be verified against the current
Teensy firmware before extending them.

FX audio files:
BOOT.WAV
READY.WAV
TRACKON.WAV
TRACKOFF.WAV
CHARGE.WAV
PLASMA.WAV
COOL.WAV
RADIO.WAV
WARN.WAV
SELECT.WAV

## Ambient Audio

AMBIENT.WAV

Separate playback channel.
Ambient mixer gain: 0.35

Commands:
AMBIENT:ON
AMBIENT:OFF
AMBIENT:STATUS

## Hardware-Tested Features

- Voice modulation
- Radio WAV playback
- Music volume adjustment
- SFX_TEST -> READY.WAV
- Tactical FX direct Teensy playback
- Ambient playback after gain adjustment

## Pending End-to-End Verification

- Wireless tactical FX buttons
- Wireless ambient toggle and acknowledgments
- Automatic tracking audio cues
- Full regression test after latest firmware uploads
- Long-duration simultaneous audio playback

## Development Rules

- One authoritative main.cpp per board.
- Commit source and documentation.
- Do not commit copyrighted music or build outputs.
- Preserve working voice and radio settings.
- Compile before flashing.
- Test hardware before marking features verified.
- Develop new subsystems on dedicated Git branches.
