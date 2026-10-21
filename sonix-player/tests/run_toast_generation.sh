#!/bin/bash
# Run from sonix-player. Uses read-only LVGL host objects built with this lv_conf.h.
# Usage: tests/run_toast_generation.sh /absolute/lvgl-objects /absolute/output
# SONIX_TEST_FONT_DIR must contain ui-regular.ttf and ui-semibold.ttf.
# Optional: TOAST_SOURCE=/absolute/baseline.c CASE=stale-body SANITIZE=1
set -euo pipefail
objects_root=${1:?LVGL object directory required}
out=${2:?Output directory required}
test -f "${SONIX_TEST_FONT_DIR:?Set approved UI font directory}/ui-regular.ttf"
mkdir -p "$out/obj"
mapfile -t objects < <(find "$objects_root" -name '*.o' -type f)
test ${#objects[@]} -gt 0
flags=(-g -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE=1 -I. -Isrc/gui/shell -I/usr/include/freetype2 -I/usr/include/SDL2)
linkflags=(-Wl,--gc-sections)
if [[ ${SANITIZE:-0} == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
    linkflags+=(-fsanitize=address,undefined)
fi
if [[ -n ${TOAST_SOURCE:-} ]]; then
    flags+=("-DTOAST_SOURCE=\"$TOAST_SOURCE\"")
fi
sources=(tests/toast_generation_test.c src/gui/shell/sx_styles.c src/gui/shell/spinner.c src/gui/shell/icons.c src/system/image/png_write.c)
for src in "${sources[@]}"; do
    gcc "${flags[@]}" -c "$src" -o "$out/obj/$(basename "$src").o"
done
g++ -o "$out/toast-test" "$out"/obj/*.o "${objects[@]}" "${linkflags[@]}" $(sdl2-config --libs) -lpthread -lm -ldl -lasound $(pkg-config --libs freetype2)
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
for height in 800 720; do
    for theme in light dark; do
        mkdir -p "$out/$height-$theme"
        "$out/toast-test" "$height" "$theme" "$out/$height-$theme" ${CASE:+"$CASE"}
    done
done
