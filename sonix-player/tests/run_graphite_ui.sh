#!/bin/bash
# Run after make host UI_TEST=1. Requires root for private mount namespaces.
# Usage: bash tests/run_graphite_ui.sh /absolute/output-directory
set -euo pipefail
player_root=$(cd "$(dirname "$0")/.." && pwd)
repo_root=$(dirname "$player_root")
output=$(realpath -m "${1:?Supply a disposable output directory}")
test_root=$(mktemp -d /tmp/sonix-graphite-test.XXXXXX)
mkdir -p "$output" /tmp/.local /tmp/sonix /mnt/sd_0
python3 "$player_root/tests/graphite_fixtures.py" "$test_root"
mock_port=${SONIX_UI_TEST_PORT:-18766}
[[ "$mock_port" =~ ^[0-9]+$ ]] && (( mock_port >= 1024 && mock_port <= 65535 ))
export ZVUK_MOCK_MODE="$test_root/mock-mode"
export SONIX_UI_TEST_MOCK_MODE="$ZVUK_MOCK_MODE"
printf 'ok\n' > "$ZVUK_MOCK_MODE"
python3 "$player_root/tools/zvuk_mock.py" "$test_root" "$mock_port" > "$output/zvuk-mock.log" 2>&1 &
mock_pid=$!
trap 'kill "$mock_pid" 2>/dev/null || true' EXIT
sleep 1
kill -0 "$mock_pid" # A conflicting port must fail, never reuse someone else's service.

for height in 800 720; do
    printf 'ok\n' > "$ZVUK_MOCK_MODE"
    run_root="$test_root/$height"
    mkdir -p "$run_root/runtime/usr" "$run_root/local" "$run_root/tmp" "$output/$height"
    cp -a "$test_root/card" "$run_root/card"
    cp "${SONIX_UI_TEST_BINARY:-$player_root/sonix_player_host_ui_test}" "$run_root/runtime/sonix_ui_test"
    model=R1
    if [ "$height" = 720 ]; then model=R3PII; fi
    cp -a "$repo_root/sonix-packer/assets/$model/usr/resource" "$run_root/runtime/usr/"
    if [ -n "${SONIX_UI_FONTS_DIR:-}" ]; then
        for face in ui-regular.ttf ui-semibold.ttf; do
            cp "$SONIX_UI_FONTS_DIR/$face" "$run_root/runtime/usr/resource/sonix/fonts/$face"
        done
    fi
    cat > "$run_root/config.ini" <<'EOF'
[ui]
language = Russian
[clock]
configured = true
[screen]
statusbar_always = 1
[power]
screen_off_seconds = 0
[zvuk]
base_url = http://127.0.0.1:18766
EOF
    sed -i "s/127.0.0.1:18766/127.0.0.1:$mock_port/" "$run_root/config.ini"
    if [ "${SONIX_UI_TEST_HOME_LAYOUT:-}" = 1 ]; then
        printf '\n[home]\ntiles = zvuk,search,music_albums,music_browse,continue_book\n' >> "$run_root/config.ini"
    fi
    if [ "${SONIX_UI_TEST_LIST_ART:-}" = 1 ]; then
        printf '\n[streaming]\nlist_art = 1\n' >> "$run_root/config.ini"
    fi
    unset SONIX_UI_TEST_LEGACY
    if [ -n "${SONIX_UI_TEST_GRAY:-}" ]; then
        sed -i '/^language = Russian$/a accent = 6' "$run_root/config.ini"
    elif [ "$height" = 720 ]; then
        sed -i '/^language = Russian$/a accent = 0\ndynamic_tint = 1' "$run_root/config.ini"
        export SONIX_UI_TEST_LEGACY=1
    fi
    printf 'pcm.!default { type null }\n' > "$run_root/alsa.conf"
    export SONIX_TEST_ROOT="$run_root" SONIX_TEST_OUTPUT="$output/$height" SONIX_TEST_HEIGHT="$height"
    unshare -m --propagation private /bin/bash <<'SH'
set -eu
mount --bind "$SONIX_TEST_ROOT/local" /tmp/.local
mount --bind "$SONIX_TEST_ROOT/card" /mnt/sd_0
mount --bind "$SONIX_TEST_ROOT/tmp" /tmp/sonix
cd "$SONIX_TEST_ROOT/runtime"
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
export ALSA_CONFIG_PATH="$SONIX_TEST_ROOT/alsa.conf"
export SONIX_PANEL="480x$SONIX_TEST_HEIGHT" SONIX_NO_MOUNT=1 SONIX_NO_SYSSERVER=1
export SONIX_SD_ROOT="$SONIX_TEST_ROOT/card" SONIX_CONFIG="$SONIX_TEST_ROOT/config.ini"
export SONIX_UI_TEST_DIR="$SONIX_TEST_OUTPUT" SONIX_UI_TEST_ZVUK=1
export SONIX_UI_TEST_TRACK="$SONIX_TEST_ROOT/card/Graphite/Time.flac"
export SONIX_UI_TEST_BOOK=$(find "$SONIX_TEST_ROOT/card/Audiobooks" -name "Home resume.flac" -print -quit)
export SONIX_UI_TEST_NEXT="$SONIX_TEST_ROOT/card/Graphite/Echoes.flac"
timeout -k 5s 240s ./sonix_ui_test > "$SONIX_TEST_OUTPUT/run.log" 2>&1
SH
    grep 'UI_TEST_PASS' "$output/$height/run.log"
done
printf 'Native UI evidence: %s\nDisposable fixtures retained: %s\n' "$output" "$test_root"
