"""Build/run normal spoken API, lazy-list and local-download fixtures on Linux."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--headers", default=".", help="checkout containing lvgl/lvgl.h")
parser.add_argument("--mp3", help="normal MP3 fixture; otherwise generate a short tone")
args = parser.parse_args()
flags = ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-fsanitize=address,undefined",
         "-DSONIX_ZVUK", "-DHOST_BUILD", "-D_FILE_OFFSET_BITS=64", "-DLV_CONF_INCLUDE_SIMPLE",
         "-ffunction-sections", "-fdata-sections", "-I.", "-I" + args.headers]
with tempfile.TemporaryDirectory(prefix="sonix-spoken-fixtures-") as temporary:
    root = Path(temporary)
    mp3 = Path(args.mp3) if args.mp3 else root / "normal.mp3"
    if not args.mp3:
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
                        "sine=frequency=440:duration=0.2", "-ac", "2", "-codec:a", "libmp3lame", str(mp3)], check=True)
    cases = [
        ("zvuk_spoken_test", ["src/system/core/json.c"], []),
        ("zvuk_spoken_list_test", [], []),
        ("zvuk_spoken_download_test", ["src/system/streaming/bookdownload.c", "src/system/streaming/podcastimport.c"], ["-Wl,--wrap=close", "-Wl,--wrap=rename"]),
    ]
    for name, sources, extra in cases:
        binary = root / name
        subprocess.run([*flags, f"tests/{name}.c", *sources, *extra, "-Wl,--gc-sections", "-lpthread", "-lm", "-o", str(binary)], check=True)
        command = [str(binary)]
        if name.endswith("download_test"):
            folder = root / "download"
            folder.mkdir()
            command += [str(mp3), str(folder)]
        subprocess.run(command, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"}, check=True)
