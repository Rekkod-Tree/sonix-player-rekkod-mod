"""Run from sonix-player after building host LVGL objects; no network/device."""
import os
from pathlib import Path
import shlex
import subprocess

objects = sorted(str(p) for p in Path("build_host/lvgl").rglob("*.o"))
assert objects, "Build host LVGL objects first"
libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "sdl2", "freetype2"], text=True))
command = ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-fsanitize=address,undefined",
           "-fno-omit-frame-pointer", "-DHOST_BUILD", "-DLV_CONF_INCLUDE_SIMPLE", "-I.", "-Ilvgl",
           "tests/streamrowart_test.c", *objects, *libs, "-pthread", "-lm", "-o", "/tmp/streamrowart_test"]
subprocess.run(command, check=True)
subprocess.run(["/tmp/streamrowart_test"], env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}, check=True)
