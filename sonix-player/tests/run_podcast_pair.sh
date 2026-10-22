#!/bin/bash
# From sonix-player: tests/run_podcast_pair.sh /absolute/private-output
# All fixture cards are fresh /tmp directories; no device/network.
set -euo pipefail
out=${1:?Private output directory required}
mkdir -p "$out"
flags=(-std=gnu11 -D_FILE_OFFSET_BITS=64 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I.)
link=(-Wl,--gc-sections -pthread)
if [[ ${SANITIZE:-1} == 1 ]]; then
 flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
 link+=(-fsanitize=address,undefined)
fi
for name in podcast_pair_publish podcast_pair_readiness_barrier podcast_download_storage podcast_download_storage_barrier podcast_pair_recovery_adapter podcast_native_identity; do
 gcc "${flags[@]}" "tests/${name}_test.c" src/system/core/sha256.c "${link[@]}" -o "$out/$name"
 ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} "$out/$name" > "$out/$name.log" 2>&1
 cat "$out/$name.log"
done
python3 tests/podcast_recovery_flow_fixture.py "$out/flow-test.c"
gcc "${flags[@]}" "$out/flow-test.c" "${link[@]}" -o "$out/flow-test"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} "$out/flow-test" > "$out/flow-test.log" 2>&1
cat "$out/flow-test.log"

python3 tests/podcast_shelf_recovery_fixture.py "$out/shelf-test.c"
gcc "${flags[@]}" "$out/shelf-test.c" src/system/streaming/podcastpair.c src/system/core/sha256.c "${link[@]}" -o "$out/shelf-test"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} "$out/shelf-test" > "$out/shelf-test.log" 2>&1
cat "$out/shelf-test.log"
