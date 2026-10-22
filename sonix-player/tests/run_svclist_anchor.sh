#!/bin/bash
# Run from sonix-player; cached actual LVGL objects are read-only.
# Usage: SONIX_TEST_FONT_DIR=/path/fonts tests/run_svclist_anchor.sh /path/lvgl-objects /path/output
set -euo pipefail
objects_root=${1:?LVGL host objects directory required}
out=${2:?Output directory required}
test -f "${SONIX_TEST_FONT_DIR:?Set UI font directory}/ui-regular.ttf"
mkdir -p "$out/obj"
mapfile -t objects < <(find "$objects_root" -name '*.o' -type f)
test ${#objects[@]} -gt 0
flags=(-g -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DHOST_BUILD -DLV_CONF_INCLUDE_SIMPLE=1 -I. -I/usr/include/freetype2 -I/usr/include/SDL2)
linkflags=(-Wl,--gc-sections)
if [[ ${SANITIZE:-1} == 1 ]]; then
 flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
 linkflags+=(-fsanitize=address,undefined)
fi
python3 tests/stream_browse_history_fixture.py "$out"
gcc "${flags[@]}" -c src/gui/shell/sx_styles.c -o "$out/obj/styles.o"
for name in svclist yandex zvuk; do
 if [[ $name == svclist ]]; then source_file=tests/svclist_anchor_test.c; else source_file="$out/$name-history-test.c"; fi
 gcc "${flags[@]}" -c "$source_file" -o "$out/obj/$name.o"
 g++ -o "$out/$name-test" "$out/obj/$name.o" "$out/obj/styles.o" "${objects[@]}" "${linkflags[@]}" $(sdl2-config --libs) -lpthread -lm -ldl -lasound $(pkg-config --libs freetype2)
 for height in 800 720; do ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} "$out/$name-test" "$height"; done
done
