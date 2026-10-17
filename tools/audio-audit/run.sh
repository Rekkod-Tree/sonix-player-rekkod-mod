#!/usr/bin/env bash
# Host-only numerical checks. No playback, ADB, or writes to a player.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
presets="$root/presets/peq/soundcore-space-one-pro"
work="$(mktemp -d)"
trap 'rm -f "$work/dsp_probe" "$work/naturalness_probe" "$work/body_voice_probe" "$work/body_voice_natural_probe"; rmdir "$work"' EXIT
cd "$root/sonix-player"
for name in dsp_probe naturalness_probe body_voice_probe body_voice_natural_probe; do
    cc -std=gnu11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections -I. \
        "$root/tools/audio-audit/$name.c" -Wl,--gc-sections -lm -o "$work/$name"
done
"$work/dsp_probe"
"$work/dsp_probe" "$presets/Space One Pro - Voice test.txt"
"$work/naturalness_probe" "$presets/Space One Pro - Voice test.txt" \
    "$presets/Space One Pro - Body test.txt" "$presets/Space One Pro - Natural test.txt"
"$work/body_voice_probe" "$presets/Space One Pro - Body Voice test.txt"
"$work/body_voice_natural_probe" "$presets/Space One Pro - Body Voice Natural test.txt"
