#!/usr/bin/env bash
# Explicitly restore the full image captured before the connection checks.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
sha256sum -c backups/original-flash-20261006.bin.sha256
pio_core="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"
exec "$pio_core/penv/bin/python" "$pio_core/packages/tool-esptoolpy/esptool.py" \
  --chip esp32s3 --port /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 \
  --baud 460800 write_flash 0x0 backups/original-flash-20261006.bin

