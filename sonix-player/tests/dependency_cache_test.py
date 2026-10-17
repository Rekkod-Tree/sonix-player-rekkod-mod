#!/usr/bin/env python3
"""Run from sonix-player; optional make overrides include LVGL_DIR=/path/to/lvgl.

LVGL must already exist. This checks flags and stamps without building libraries.
"""

from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    root = Path.cwd()
    overrides = sys.argv[1:]
    lvgl = next((Path(arg.split("=", 1)[1]) for arg in overrides if arg.startswith("LVGL_DIR=")), root / "lvgl")
    assert (root / "Makefile").is_file() and lvgl.is_dir()
    with tempfile.TemporaryDirectory(prefix="sonix-dependency-cache-") as directory:
        stamp = Path(directory) / "flags"

        def flags(*changes):
            subprocess.run(
                ["make", "--no-print-directory", "-s", str(stamp),
                 f"TARGET_FLAGS_STAMP={stamp}", *overrides, *changes], check=True,
            )
            return stamp.read_text(), stamp.stat().st_mtime_ns

        current, first_time = flags()
        assert "-flto=4" in current, "target firmware must enable LTO by default"
        assert "/freetype-target/2.14.3/" in current
        assert "/audio-target/ogg-1.3.6-opus-1.6.1-opusfile-0.12-wv-5.9.0/" in current
        time.sleep(0.02)
        assert flags() == (current, first_time), "unchanged build must not invalidate objects"

        for override in ("FREETYPE_VER=2.13.3", "OPUS_VER=1.4", "WAVPACK_VER=5.6.0", "TARGET_LTO=0"):
            time.sleep(0.02)
            changed, changed_time = flags(override)
            assert changed != current and changed_time != first_time
            if override == "TARGET_LTO=0":
                assert "-flto" not in changed and "-flto=4" in current
                time.sleep(0.02)
                assert flags(override) == (changed, changed_time), "unchanged diagnostic build must not rebuild objects"
            time.sleep(0.02)
            restored, restored_time = flags()
            assert restored == current and restored_time != changed_time
            first_time = restored_time

        variables = ("HOST_CFLAGS", "HOST_CXXFLAGS", "HOST_LDFLAGS", "LAUNCH_CFLAGS",
                     "LIB_CFLAGS", "OPUS_CFLAGS", "OPUS_CONFIGURE", "WAVPACK_CONFIGURE",
                     "FREETYPE_PREFIX", "AUDIO_PREFIX", "TARGET_CFLAGS", "TARGET_CXXFLAGS", "TARGET_LDFLAGS")
        query = Path(directory) / "inspect.mk"
        query.write_text(".PHONY: inspect-lto-flags\ninspect-lto-flags:\n\t@printf '%s\\n' " +
                         " ".join(f"'$({name})'" for name in variables) + "\n")

        def inspect(enabled):
            output = subprocess.check_output(
                ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", str(query),
                 *overrides, f"TARGET_LTO={enabled}", "inspect-lto-flags"], text=True,
            )
            values = output.splitlines()
            assert len(values) == len(variables)
            return dict(zip(variables, values))

        baseline, lto = inspect(0), inspect(1)
        for name in variables[:10]:
            assert baseline[name] == lto[name], f"LTO changed non-player flags/provenance: {name}"
        for name in variables[10:]:
            assert "-flto" not in baseline[name] and "-flto=4" in lto[name], name
            assert "-Ofast" not in lto[name] and "-ffast-math" not in lto[name], name
            for option in ("-march=mips32r2", "-mno-madd4", "-mplt"):
                assert option in lto[name], (name, option)
        levels = [arg for arg in lto["TARGET_LDFLAGS"].split() if arg.startswith("-O")]
        assert levels == ["-O2"], "LTO final optimization level must be explicit O2"
        assert "-flto-partition=one" in lto["TARGET_LDFLAGS"].split(), "math symbol bindings require one final LTRANS unit"
        for name in ("TARGET_CFLAGS", "TARGET_CXXFLAGS"):
            assert "-flto-partition=" not in lto[name], "partition policy belongs only to the final link"

        # Question mode runs no recipes. Empty object/archive fixtures isolate
        # whether a Makefile link-policy change invalidates an existing binary.
        archive = Path(directory) / "ft/lib/libfreetype.a"
        archive.parent.mkdir(parents=True)
        archive.touch()
        binary = Path(directory) / "player"
        binary.touch()
        question = ["make", "--no-print-directory", "-q", "-o", str(archive), *overrides,
                    f"TARGET_BIN={binary}", "TARGET_OBJS=", f"FREETYPE_PREFIX={archive.parent.parent}",
                    "AUDIO_LIBS=", str(binary)]
        assert subprocess.run(question).returncode == 0, "unchanged target should not relink"
        assert subprocess.run([*question, "-W", "Makefile"]).returncode == 1, "link-policy changes must relink the target"
    print("PASS: library/LTO toggle and rollback invalidate target; host, launcher and dependency flags unchanged")


if __name__ == "__main__":
    main()
