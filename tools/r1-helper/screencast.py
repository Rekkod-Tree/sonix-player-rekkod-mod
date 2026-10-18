"""Record real R1 clips; optional single gesture requires a fresh snapshot and lease."""
from array import array
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import uuid

from PIL import Image
from r1 import R1, HERE


def decode(data: bytes):
    if len(data) < 16 or len(data) > 3 * 1024 * 1024 or data[:8] != b"SNXR1V1\n" or struct.unpack_from("<II", data, 8) != (480, 800):
        raise ValueError("Unsupported recording")
    at, frame = 16, array("H", [0]) * (480 * 800)
    count_frames = 0
    while at < len(data):
        count_frames += 1
        if count_frames > 144: raise ValueError("Too many frames in a bounded R1 recording")
        if at + 8 > len(data): raise ValueError("Truncated frame header")
        timestamp, length = struct.unpack_from("<II", data, at)
        at += 8
        if length > 480 * 800 * 4 or length % 2 or at + length > len(data):
            raise ValueError("Invalid frame payload")
        words = array("H")
        words.frombytes(data[at:at + length])
        if sys.byteorder != "little": words.byteswap()
        at += length
        n, pixel = 0, 0
        while n < len(words):
            token = words[n]; n += 1
            count = (token & (0x7fff if token < 0x8000 else 0x3fff)) + 1
            if pixel + count > len(frame): raise ValueError("Frame overrun")
            if token >= 0xc000:
                if n + count > len(words): raise ValueError("Truncated literals")
                frame[pixel:pixel + count] = words[n:n + count]
                n += count
            elif token >= 0x8000:
                if n == len(words): raise ValueError("Truncated repetition")
                frame[pixel:pixel + count] = array("H", [words[n]]) * count
                n += 1
            pixel += count
        if pixel != len(frame): raise ValueError("Incomplete frame")
        pixels = array("H", frame)
        if sys.byteorder != "little": pixels.byteswap()
        yield timestamp, Image.frombytes("RGB", (480, 800), pixels.tobytes(), "raw", "BGR;16")


def export_frames(data: bytes, directory: Path, fps: int = 10):
    lines = ["ffconcat version 1.0"]
    count, first, previous_ms, previous_name = 0, None, None, None
    for timestamp, image in decode(data):
        name = f"frame-{count:04}.png"
        image.save(directory / name)
        image.close()
        if first is None: first = timestamp
        if previous_name is not None:
            if timestamp <= previous_ms: raise ValueError("Recording timestamps must increase")
            lines += [f"file '{previous_name}'", f"duration {(timestamp - previous_ms) / 1000:.6f}"]
        previous_ms, previous_name = timestamp, name
        count += 1
    if not count: raise ValueError("Empty recording")
    lines += [f"file '{previous_name}'", f"duration {1/fps:.6f}", f"file '{previous_name}'"]
    (directory / "frames.ffconcat").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return count, first, previous_ms


def record(directory: Path, seconds: int, fps: int, gesture=None, snapshot="", token=""):
    directory.mkdir(parents=True, exist_ok=False)
    device = R1()
    if not device.status()["display_available"]:
        raise ValueError("Display unavailable; recorder does not wake it")
    binary = HERE / "bin/record"
    sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    remote = "/tmp/sonix-r1-record-" + sha[:12]
    output = "/tmp/sonix-r1-video-" + uuid.uuid4().hex
    remote_log = output + ".log"
    try:
        device.adb_call("push", str(binary), remote)
        device.adb_call("shell", f"chmod 700 {remote}")
        if device.adb_call("shell", f"sha256sum {remote}").decode().split()[0] != sha:
            raise ValueError("Recorder transfer hash mismatch")
        print("RECORDING", directory, flush=True)
        if gesture:
            if len(gesture) not in (3, 5): raise ValueError("Expected tap or swipe coordinates and duration")
            x, y = gesture[:2]
            if not (0 <= x < 480 and 0 <= y < 800): raise ValueError("Input outside panel")
            if len(gesture) == 5 and not (0 <= gesture[2] < 480 and 0 <= gesture[3] < 800): raise ValueError("Swipe outside panel")
            if not 30 <= gesture[-1] <= 1500: raise ValueError("Invalid duration")
            with device.input_lock():
                device.guard(snapshot, token, (0, 35, 480, 800))
                touch = device.remote_helper("touch") + " " + " ".join(map(str, gesture))
                command = (f"{remote} {seconds} {fps} {output} > {remote_log} 2>&1 & recorder=$!; "
                           f"sleep 1; inputcode=99; if test -s {output}; then {touch}; inputcode=$?; fi; "
                           f'wait "$recorder"; cat {remote_log}; printf "SONIX_INPUT_STATUS=%s\\n" "$inputcode"')
                result = device.adb_call("shell", command, timeout=seconds + 20).decode(errors="replace")
                if "SONIX_INPUT_STATUS=0" not in result: raise ValueError("Recorded input failed; no success claimed")
        else:
            result = device.adb_call("shell", f"{remote} {seconds} {fps} {output}", timeout=seconds + 20).decode(errors="replace")
        device.adb_call("pull", output, str(directory / "capture.snx"))
        (directory / "capture-result.txt").write_text(result, encoding="utf-8")
        if "status=0" not in result:
            raise ValueError("Recording stopped early; inspect capture-result.txt")
    finally:
        device.adb_call("shell", f"rm -f {remote} {output} {remote_log}")
    count, first, last = export_frames((directory / "capture.snx").read_bytes(), directory, fps)
    (directory / "recording.json").write_text(json.dumps({"source": "real HiBy R1 framebuffer", "frames": count,
        "first_ms": first, "last_ms": last, "fps_requested": fps, "seconds_requested": seconds,
        "native_sha256": sha, "audio": False, "device_file_cap": 3 * 1024 * 1024}, indent=2), encoding="utf-8")
    print("COMPLETE", count, "frames", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--seconds", type=int, choices=range(1, 13), default=6)
    parser.add_argument("--fps", type=int, choices=range(1, 13), default=8)
    parser.add_argument("--background", action="store_true", help="Start a hidden local recorder and return its PID")
    parser.add_argument("--gesture", nargs="+", type=int, help="One explicitly requested tap or swipe, requiring snapshot and lease")
    parser.add_argument("--snapshot", default="")
    parser.add_argument("--token", default="")
    options = parser.parse_args()
    if options.background:
        if options.gesture: raise ValueError("Use synchronous recording for controlled input")
        options.directory.parent.mkdir(parents=True, exist_ok=True)
        log_path = options.directory.with_suffix(".host.log")
        with log_path.open("wb") as log:
            child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), str(options.directory),
                "--seconds", str(options.seconds), "--fps", str(options.fps)], stdout=log, stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        print(json.dumps({"pid": child.pid, "log": str(log_path)}))
    else:
        record(options.directory, options.seconds, options.fps, options.gesture, options.snapshot, options.token)
