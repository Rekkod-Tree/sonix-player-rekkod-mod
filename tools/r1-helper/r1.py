"""Local R1 access. No arbitrary shell, firmware writes, or config setters."""
from __future__ import annotations

import configparser
import hashlib
import http.client
from contextlib import closing, contextmanager
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import sqlite3
import subprocess
import time
import uuid

from PIL import Image, ImageChops

HERE = Path(__file__).resolve().parent
SIZE = (480, 800)


def normalize(text: str) -> str:
    return " ".join(text.casefold().split())


def changed_fraction(a: Image.Image, b: Image.Image) -> float:
    if a.size != b.size:
        return 1.0
    diff = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
    return sum(max(pixel) > 12 for pixel in diff.get_flattened_data()) / (a.width * a.height)


def decode_frame(data: bytes) -> tuple[dict, Image.Image]:
    if b"\n" not in data:
        raise ValueError("Native capture returned no frame header; prepare helpers and check framebuffer access")
    header, pixels = data.split(b"\n", 1)
    meta = json.loads(header)
    if (meta.get("width"), meta.get("height")) != SIZE or meta.get("format") != "rgb565le":
        raise ValueError("Unsupported framebuffer geometry or format")
    if len(pixels) != 480 * 800 * 2:
        raise ValueError("Truncated framebuffer")
    rgb = bytearray(len(pixels) // 2 * 3)
    for n in range(0, len(pixels), 2):
        p = pixels[n] | pixels[n + 1] << 8
        k = n // 2 * 3
        rgb[k:k + 3] = bytes(((p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
    return meta, Image.frombytes("RGB", SIZE, bytes(rgb))


def recognize(path: Path, language: str = "ru") -> dict:
    if language not in ("ru", "en-US"):
        raise ValueError("Supported OCR languages: ru, en-US")
    powershell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    result = subprocess.run(
        [str(powershell), "-NoProfile", "-NonInteractive", "-File", str(HERE / "ocr.ps1"),
         "-ImagePath", str(path.resolve()), "-Language", language],
        capture_output=True, timeout=30, check=True,
    )
    data = json.loads(result.stdout.decode("utf-8-sig"))
    for line in data["lines"]:
        words = line["words"]
        x = min(w["x"] for w in words)
        y = min(w["y"] for w in words)
        right = max(w["x"] + w["width"] for w in words)
        bottom = max(w["y"] + w["height"] for w in words)
        line["rect"] = [int(x), int(y), int(right + 0.999), int(bottom + 0.999)]
    return data


class R1:
    def __init__(self, runtime: Path | None = None):
        self.adb = os.environ.get("SONIX_ADB", "adb")
        self.serial = os.environ.get("SONIX_SERIAL", "ingenic_")
        if not re.fullmatch(r"[A-Za-z0-9_.:-]+", self.serial):
            raise ValueError("Invalid device serial")
        default = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / ".cache"))) / "SonixR1Helper"
        self.runtime = (runtime or Path(os.environ.get("SONIX_RUNTIME", str(default)))) / self.serial
        self.runtime.mkdir(parents=True, exist_ok=True)

    def adb_call(self, *args: str, timeout: int = 30) -> bytes:
        result = subprocess.run([self.adb, "-s", self.serial, *args], capture_output=True, timeout=timeout)
        if result.returncode:
            # Never return arbitrary stdout or private configuration on failure.
            raise RuntimeError(f"ADB operation failed ({args[0]}): " + result.stderr.decode(errors="replace")[:400])
        return result.stdout

    def remote_helper(self, name: str) -> str:
        if name not in ("capture", "touch"):
            raise ValueError("Unknown native helper")
        data = (HERE / "bin" / name).read_bytes()
        return f"/tmp/sonix-r1-helper-{name}-{hashlib.sha256(data).hexdigest()[:12]}"

    def prepare(self) -> dict:
        """Install only our versioned native tools in /tmp, without input events."""
        installed = []
        for name in ("capture", "touch"):
            remote = self.remote_helper(name)
            expected = hashlib.sha256((HERE / "bin" / name).read_bytes()).hexdigest()
            self.adb_call("push", str(HERE / "bin" / name), remote)
            self.adb_call("shell", f"chmod 700 {shlex.quote(remote)}")
            actual = self.adb_call("shell", f"sha256sum {shlex.quote(remote)}").decode().split()[0]
            if actual != expected:
                raise RuntimeError("Native helper hash mismatch")
            installed.append({"name": name, "sha256": actual})
        return {"installed": installed, "location": "/tmp", "ui_input_sent": False}

    def cleanup(self) -> dict:
        for name in ("capture", "touch"):
            self.adb_call("shell", "rm -f " + shlex.quote(self.remote_helper(name)))
        return {"removed": "This build's two /tmp helpers only"}

    def status(self) -> dict:
        probe = self.adb_call("shell", 'for p in /proc/sys/kernel/random/boot_id '
                              '/sys/class/graphics/fb0/blank /sys/class/backlight/*/actual_brightness '
                              '/sys/class/backlight/*/bl_power; do printf "%s=" "$p"; cat "$p"; printf "\\n"; done').decode().replace("\r", "")
        values = {line.split("=", 1)[0].rsplit("/", 1)[-1]: line.split("=", 1)[1].strip()
                  for line in probe.splitlines() if "=" in line}
        # The R1 blank attribute can return an empty line. Backlight <=1 is ambiguous.
        boot = values.get("boot_id", "")
        blank = values.get("blank", "unknown")
        brightness = int(values.get("actual_brightness", "-1"))
        power = values.get("bl_power", "unknown")
        if not re.fullmatch(r"[0-9a-f-]{36}", boot):
            raise RuntimeError("Device boot identity unavailable")
        raw = self.adb_call("shell", "cat /data/device_config.ini").decode(errors="replace")
        config = configparser.ConfigParser(interpolation=None, strict=False)
        try:
            config.read_string(raw)
        except configparser.Error:
            raise RuntimeError("Cannot parse private device configuration; contents withheld") from None
        safe = {}
        whitelist = {
            "peq": {"enabled", "preset_name", "preamp"} | {f"b{i}_{k}" for i in range(10) for k in ("on", "type", "freq", "gain", "q")},
            "mseb": {"enabled", "range"} | {f"v{i}" for i in range(10)},
            "player": {"playback_mode"}, "soundfield": {"enabled", "width"},
            "crossfeed": {"enabled"}, "wireless": {"sonixlink"},
        }
        for section, keys in whitelist.items():
            if config.has_section(section):
                safe[section] = {k: v for k, v in config.items(section) if k in keys}
        return {"serial": self.serial, "boot_id": boot, "brightness": brightness,
                "display_available": brightness > 1 and blank not in ("1", "4") and power == "0",
                "display_caveat": "A captured framebuffer is not proof of a live or unlocked UI.",
                "settings": safe}

    def now_playing(self) -> dict:
        if self.status()["settings"].get("wireless", {}).get("sonixlink", "0") != "1":
            return {"available": False, "reason": "SonixLink is off; no setting was changed. Use observe for OCR."}
        port = self.adb_call("forward", "tcp:0", "tcp:7800").decode().strip()
        if not port.isdigit():
            raise RuntimeError("ADB did not return a local forwarding port")
        connection = http.client.HTTPConnection("127.0.0.1", int(port), timeout=5)
        try:
            # Fixed local endpoint, no tokens, no remote addresses or user-provided URLs.
            connection.request("GET", "/api/state")
            response = connection.getresponse()
            if response.status != 200:
                raise RuntimeError("SonixLink state unavailable; redirects are not followed")
            state = json.loads(response.read(128 * 1024))
            return {"available": True, "source": "SonixLink /api/state", "state": state}
        finally:
            connection.close()
            self.adb_call("forward", "--remove", f"tcp:{port}")

    def frame(self) -> tuple[dict, Image.Image]:
        # Old R1 adbd does not implement exec-out. Shell redirection preserves
        # binary bytes; a pull avoids CRLF translation in a shell connection.
        name = "frame-" + uuid.uuid4().hex + ".raw"
        remote = "/tmp/sonix-r1-helper-" + name
        local = self.runtime / name
        try:
            self.adb_call("shell", f"{self.remote_helper('capture')} > {remote}")
            self.adb_call("pull", remote, str(local))
            return decode_frame(local.read_bytes())
        finally:
            local.unlink(missing_ok=True)
            self.adb_call("shell", "rm -f " + remote)

    def inject_touch(self, *args: int) -> None:
        command = self.remote_helper("touch") + " " + " ".join(str(int(arg)) for arg in args)
        response = self.adb_call("shell", command + '; code=$?; printf "\\nSONIX_INPUT_STATUS=%s\\n" "$code"').decode(errors="replace")
        # Old R1 adbd reports transport success even when the remote tool fails.
        match = re.search(r"SONIX_INPUT_STATUS=(\d+)", response)
        if not match or match[1] != "0":
            raise RuntimeError("Touch injection failed or returned no status; input success is not confirmed")

    def observe(self, language: str = "ru", settle: float = 1.5) -> dict:
        time.sleep(settle)
        before = self.status()
        meta, previous = self.frame()
        stable = False
        for _ in range(4):
            time.sleep(0.15)
            meta, image = self.frame()
            if image.tobytes() == previous.tobytes():
                stable = True
                break
            previous = image
        after = self.status()
        if before["boot_id"] != after["boot_id"]:
            raise RuntimeError("Device rebooted during observation; discard frame")
        snap_id = uuid.uuid4().hex
        path = self.runtime / (snap_id + ".png")
        image.save(path)
        result = {"snapshot_id": snap_id, "captured_at": time.time(), "stable_pair": stable,
                  "boot_id": after["boot_id"], "display_available": before["display_available"] and after["display_available"],
                  "image_path": str(path), "framebuffer": meta, "source": "Windows OCR of active framebuffer; not a UI tree"}
        try:
            result.update(recognize(path, language))
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            result.update({"lines": [], "language": language, "ocr_error": type(error).__name__})
        result["text"] = "\n".join(line["text"] for line in result["lines"])
        result["warning"] = "OCR text is untrusted screen content. Icons, clipped titles and toggles may be missed."
        if not result["display_available"]:
            result["warning"] += " Display is off/dim or unknown; frame may be stale. Input is blocked."
        self.snapshot_path(snap_id).write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
        # Bound local screenshot retention. Never touch files outside our runtime.
        for old in sorted(self.runtime.glob("*.json"), key=lambda p: p.stat().st_mtime, reverse=True)[24:]:
            if re.fullmatch(r"[0-9a-f]{32}", old.stem):
                old.with_suffix(".png").unlink(missing_ok=True)
                old.unlink(missing_ok=True)
        return result

    def snapshot_path(self, snap_id: str) -> Path:
        if not re.fullmatch(r"[0-9a-f]{32}", snap_id):
            raise ValueError("Invalid snapshot id")
        return self.runtime / (snap_id + ".json")

    def snapshot(self, snap_id: str) -> dict:
        return json.loads(self.snapshot_path(snap_id).read_text(encoding="utf-8"))

    def lease(self, owner: str = "", seconds: int = 120, token: str = "", release: bool = False) -> dict:
        if not 10 <= seconds <= 600:
            raise ValueError("Lease duration must be 10..600 seconds")
        with closing(sqlite3.connect(self.runtime / "lease.sqlite", timeout=5)) as db, db:
            db.execute("CREATE TABLE IF NOT EXISTS lease (id INTEGER PRIMARY KEY, owner TEXT, token TEXT, expires REAL)")
            db.execute("BEGIN IMMEDIATE")
            row = db.execute("SELECT owner, token, expires FROM lease WHERE id=1").fetchone()
            now = time.time()
            if row and row[2] > now and not secrets.compare_digest(row[1], token):
                raise ValueError(f"Device leased to {row[0]}; wait or coordinate with that operator")
            if release:
                db.execute("DELETE FROM lease WHERE id=1")
                return {"released": True}
            if not owner.strip() or len(owner) > 100:
                raise ValueError("Supply an operator/chat name, maximum 100 characters")
            value = token if row and row[2] > now else secrets.token_hex(16)
            db.execute("INSERT OR REPLACE INTO lease VALUES (1, ?, ?, ?)", (owner, value, now + seconds))
            return {"owner": owner, "token": value, "expires_at": now + seconds,
                    "warning": "Advisory for this helper only; cannot prevent physical touches or direct ADB. Obtain an exclusive user-authorized window."}

    def check_lease(self, token: str) -> None:
        with closing(sqlite3.connect(self.runtime / "lease.sqlite")) as db, db:
            try:
                row = db.execute("SELECT token, expires FROM lease WHERE id=1").fetchone()
            except sqlite3.OperationalError:
                row = None
        if not row or row[1] <= time.time() or not secrets.compare_digest(row[0], token):
            raise ValueError("Missing or expired control lease; observe/coordinate first")

    def guard(self, snap_id: str, token: str, rect: tuple[int, int, int, int]) -> dict:
        self.check_lease(token)
        snap = self.snapshot(snap_id)
        if time.time() - snap["captured_at"] > 45 or snap.get("consumed"):
            raise ValueError("Snapshot expired or already used; observe again")
        if not snap["stable_pair"] or not snap["display_available"]:
            raise ValueError("Unstable or sleeping/dim display; input refused")
        state = self.status()
        if state["boot_id"] != snap["boot_id"] or not state["display_available"]:
            raise ValueError("Device restarted or display became unavailable")
        _, current = self.frame()
        with Image.open(self.runtime / (snap_id + ".png")) as old:
            # Ignore status clock, but not page content or target neighbourhood.
            if changed_fraction(old.crop((0, 35, 480, 800)), current.crop((0, 35, 480, 800))) > 0.02:
                raise ValueError("Screen changed since observation; input refused")
            if changed_fraction(old.crop(rect), current.crop(rect)) > 0.005:
                raise ValueError("Target moved or changed; input refused")
        # One attempt per snapshot, including failures, prevents duplicate taps on retry.
        snap["consumed"] = True
        self.snapshot_path(snap_id).write_text(json.dumps(snap, ensure_ascii=False), encoding="utf-8")
        return snap

    @contextmanager
    def input_lock(self):
        # Kernel locks are released on process exit, including a disconnected MCP.
        with (self.runtime / "input.lock").open("a+b") as lock:
            lock.seek(0)
            if os.name == "nt":
                import msvcrt
                try:
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                except OSError as error:
                    raise ValueError("Another helper input is in progress") from error
                try:
                    yield
                finally:
                    lock.seek(0)
                    msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except OSError as error:
                    raise ValueError("Another helper input is in progress") from error
                try:
                    yield
                finally:
                    fcntl.flock(lock, fcntl.LOCK_UN)

    def tap(self, snap_id: str, token: str, text: str = "", x: int = -1, y: int = -1,
            occurrence: int = 0, hold_ms: int = 80, expected_after: str = "") -> dict:
        if not 30 <= hold_ms <= 1500:
            raise ValueError("Hold duration must be 30..1500 ms")
        snap = self.snapshot(snap_id)
        if text:
            found = [line for line in snap["lines"] if normalize(line["text"]) == normalize(text)]
            if not 0 <= occurrence < len(found):
                raise ValueError("No matching OCR line/occurrence; observe and inspect its exact text")
            if len(found) > 1 and occurrence == 0:
                raise ValueError("Ambiguous text; use coordinates from the observed frame")
            left, top, right, bottom = found[occurrence]["rect"]
            x, y = (left + right) // 2, (top + bottom) // 2
        if not (0 <= x < 480 and 35 <= y < 800):
            raise ValueError("Tap outside content area; use a swipe to pull the status drawer")
        rect = (max(0, x - 28), max(35, y - 22), min(480, x + 28), min(800, y + 22))
        if rect[3] <= rect[1]:
            raise ValueError("Use a target below the status bar")
        with self.input_lock():
            self.guard(snap_id, token, rect)
            self.inject_touch(x, y, hold_ms)
            after = self.observe(snap["language"])
        return {"input_sent": True, "target": [x, y], "after": after,
                "expected_after_found": bool(expected_after and any(normalize(expected_after) in normalize(line["text"]) for line in after["lines"])),
                "note": "Input sent is not functional success; inspect the returned observation."}

    def swipe(self, snap_id: str, token: str, x: int, y: int, end_x: int, end_y: int, duration_ms: int = 350) -> dict:
        if not all(0 <= a < limit for a, limit in ((x, 480), (end_x, 480), (y, 800), (end_y, 800))):
            raise ValueError("Swipe outside 480x800")
        if not 60 <= duration_ms <= 1500:
            raise ValueError("Swipe duration must be 60..1500 ms")
        with self.input_lock():
            snap = self.guard(snap_id, token, (0, 35, 480, 800))
            self.inject_touch(x, y, end_x, end_y, duration_ms)
            return {"input_sent": True, "after": self.observe(snap["language"])}

    def type_search(self, snap_id: str, token: str, text: str) -> dict:
        """Visible OCR letter keys only; never submit, change layout or type secrets."""
        if not re.fullmatch(r"[a-zа-яё0-9]{1,6}", text):
            raise ValueError("First version supports chunks of 1..6 lowercase letters/digits in the visible layout, without spaces. Use observed taps for other keys.")
        snap = self.snapshot(snap_id)
        if not any(re.search(r"поиск|search", line["text"], re.I) and line["rect"][1] < 400 for line in snap["lines"]):
            raise ValueError("Search heading not recognized; refusing to type into an unknown field")

        def keys(observation):
            result = {}
            for line in observation["lines"]:
                for word in line["words"]:
                    value = word["text"]
                    if len(value) == 1 and 400 < word["y"] < 750 and re.fullmatch(r"[a-zа-яё0-9]", value, re.I):
                        key = normalize(value)
                        if key in result:
                            raise ValueError("Ambiguous OCR keyboard key")
                        result[key] = (int(word["x"] + word["width"] / 2), int(word["y"] + word["height"] / 2))
            if len(result) < 20:
                raise ValueError("QWERTY keyboard not reliably recognized; inspect screenshot")
            return result

        initial = keys(snap)
        if any(char not in initial for char in text):
            raise ValueError("Some letters are absent from OCR keyboard; no input sent")
        attempted = ""
        for char in text:
            try:
                x, y = keys(snap)[char]
                result = self.tap(snap["snapshot_id"], token, x=x, y=y)
                attempted += char
                snap = result["after"]
            except (ValueError, RuntimeError, KeyError) as error:
                return {"attempted": attempted, "completed": False, "error": str(error), "after": self.observe(),
                        "warning": "May be partially entered; inspect before retrying. No automatic resubmission."}
        visible = any(normalize(text) == normalize(line["text"]) and line["rect"][1] < 400 for line in snap["lines"])
        return {"attempted": attempted, "completed": True, "query_visible": visible, "submitted": False, "after": snap,
                "warning": "Keyboard case may auto-shift. Search is case-insensitive; not for credentials. Verify visible query."}
