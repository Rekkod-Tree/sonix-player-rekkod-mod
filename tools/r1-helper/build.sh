#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
cc="${SONIX_CC:-/root/sonix-tools/rockbox-toolchain/bin/mipsel-rockbox-linux-gnu-gcc}"
mkdir -p bin
for name in capture touch; do
    "$cc" -std=gnu11 -Os -static -Wall -Wextra -Werror "native/$name.c" -o "bin/$name"
done
