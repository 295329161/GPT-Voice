#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
python_bin="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/penv/bin/python"
case "${1:-help}" in
  build|flash|flash-usb|monitor|debug) exec "$python_bin" scripts/pio_run.py "$1" ;;
  *) printf '%s\n' 'Usage: bash dev.sh {build|flash|flash-usb|monitor|debug}' ;;
esac
