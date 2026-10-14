#!/usr/bin/env python3
"""Run from sonix-player with LVGL bootstrapped; does not rebuild libraries."""

from pathlib import Path
import subprocess
import tempfile
import time


def main():
    root = Path.cwd()
    assert (root / "Makefile").is_file() and (root / "lvgl").is_dir()
    with tempfile.TemporaryDirectory(prefix="sonix-dependency-cache-") as directory:
        stamp = Path(directory) / "flags"

        def flags(*overrides):
            subprocess.run(
                ["make", "--no-print-directory", "-s", str(stamp),
                 f"TARGET_FLAGS_STAMP={stamp}", *overrides], check=True,
            )
            return stamp.read_text(), stamp.stat().st_mtime_ns

        current, first_time = flags()
        assert "/freetype-target/2.14.3/" in current
        assert "/audio-target/ogg-1.3.6-opus-1.6.1-opusfile-0.12-wv-5.9.0/" in current
        time.sleep(0.02)
        assert flags() == (current, first_time), "unchanged build must not invalidate objects"

        for override in ("FREETYPE_VER=2.13.3", "OPUS_VER=1.4", "WAVPACK_VER=5.6.0"):
            time.sleep(0.02)
            changed, changed_time = flags(override)
            assert changed != current and changed_time != first_time
            time.sleep(0.02)
            restored, restored_time = flags()
            assert restored == current and restored_time != changed_time
            first_time = restored_time
    print("PASS: library changes and rollback invalidate consumers; unchanged builds do not")


if __name__ == "__main__":
    main()
