#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
sudo install -m 0644 "$script_dir/99-esp32s3-usb-jtag.rules" /etc/udev/rules.d/99-esp32s3-usb-jtag.rules
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=303a --attr-match=idProduct=1001
udevadm settle
printf '%s\n' 'USB JTAG permission rule installed.'
