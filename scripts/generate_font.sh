#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
npx --yes lv_font_conv@1.5.3 --font /usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf --range 0x3000-0x303f,0x4e00-0x9fff,0xff00-0xffef --size 16 --bpp 2 --format lvgl --no-compress --output src/assets/font_cjk.c --lv-font-name terminal_cjk --lv-include lvgl.h
