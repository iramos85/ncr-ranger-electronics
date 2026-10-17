# NCR Ranger — PlatformIO Development Environment

## Verified Mobile Lab Configuration

Verified: October 17, 2026

- Computer: Intel Mac
- macOS: 15.8.1
- Architecture: x86_64
- PlatformIO Core: 6.2.0
- Python: 3.11.7 (PlatformIO environment)
- PIOArduino ESP32 platform: 55.03.39
- cryptography: 48.0.1

## Known Issue: Python Dependency Installation Failure

### Symptoms

Building firmware/metro-pipboy may fail with:

    Error: Failed to install Python dependencies (exit code: 1)
    Error: Failed to install Python dependencies into penv

### Root Cause

PIOArduino attempts to install or upgrade Python dependencies.

On the Intel mobile lab, cryptography 49.0.0 and
50.0.2 attempted source builds that failed because OpenSSL
development libraries and pkg-config were unavailable.

The cryptography 48.0.1 binary package installed successfully.

### Verified Workaround

Create a temporary constraint file:

    echo 'cryptography==48.0.1' > /tmp/ncr-python-constraints.txt

Export the constraint:

    export UV_CONSTRAINT=/tmp/ncr-python-constraints.txt

Install the compatible package:

    ~/.platformio/penv/bin/uv pip install \
      --python ~/.platformio/penv/bin/python \
      --constraint /tmp/ncr-python-constraints.txt \
      'cryptography==48.0.1'

Install the other PIOArduino dependencies without --upgrade.
Use the requirements from the installed PIOArduino platform.

Build:

    cd firmware/metro-pipboy
    pio run

### Important Notes

- The constraint file is temporary and is not stored in Git.
- UV_CONSTRAINT applies to the current shell session.
- A new Terminal session may require re-exporting it.
- A fresh PlatformIO installation may require installing
  the remaining dependencies again.
- Do not downgrade the PIOArduino platform unnecessarily.
- Do not modify the firmware to resolve this environment issue.
- Do not overwrite a known-good firmware image without backup.

### Status

Metro ESP32-S3 firmware build: SUCCESS

Hardware upload and runtime verification are separate steps.
