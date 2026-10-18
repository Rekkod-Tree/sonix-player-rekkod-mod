import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from r1 import R1, decode_frame


class FakeR1(R1):
    def __init__(self, runtime):
        super().__init__(runtime)
        self.image = Image.new("RGB", (480, 800), "white")
        self.awake = True
        self.boot = "boot-a"
        self.inputs = []

    def status(self):
        return {"boot_id": self.boot, "display_available": self.awake,
                "settings": {"wireless": {"sonixlink": "0"}}}

    def frame(self):
        return {}, self.image.copy()

    def remote_helper(self, name):
        return "/tmp/test-" + name

    def adb_call(self, *args, **kwargs):
        self.inputs.append(args)
        return b"SONIX_INPUT_STATUS=0\n"

    def observe(self, language="ru", settle=0):
        snap_id = "a" * 31 + str(len(list(self.runtime.glob("*.json"))) % 10)
        path = self.runtime / (snap_id + ".png")
        self.image.save(path)
        snap = {"snapshot_id": snap_id, "captured_at": time.time(), "stable_pair": True,
                "display_available": self.awake, "boot_id": self.boot, "language": language,
                "lines": [{"text": "Музыка", "rect": [20, 100, 150, 130], "words": []}]}
        self.snapshot_path(snap_id).write_text(json.dumps(snap), encoding="utf-8")
        return snap


class GuardsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.device = FakeR1(Path(self.tmp.name))
        self.token = self.device.lease("test chat")["token"]
        self.snap = self.device.observe()

    def update(self, **changes):
        self.snap.update(changes)
        self.device.snapshot_path(self.snap["snapshot_id"]).write_text(json.dumps(self.snap), encoding="utf-8")

    def tap(self):
        return self.device.tap(self.snap["snapshot_id"], self.token, text="Музыка")

    def test_changed_target_refused_without_input(self):
        self.device.image.paste("black", (20, 100, 160, 150))
        with self.assertRaisesRegex(ValueError, "changed|moved"):
            self.tap()
        self.assertEqual(self.device.inputs, [])

    def test_sleep_reboot_and_expiry_refused(self):
        for changes in ({"display_available": False}, {"stable_pair": False}, {"captured_at": 0}, {"boot_id": "old-boot"}):
            original = self.snap.copy()
            self.update(**changes)
            with self.assertRaises(ValueError):
                self.tap()
            self.snap = original
            self.update()
        self.assertEqual(self.device.inputs, [])

    def test_missing_lease_refused(self):
        with self.assertRaisesRegex(ValueError, "lease"):
            self.device.tap(self.snap["snapshot_id"], "wrong", text="Музыка")
        self.assertEqual(self.device.inputs, [])

    def test_remote_input_failure_not_reported_as_success(self):
        for response in (b"Unsupported coordinates\nSONIX_INPUT_STATUS=2\n", b"No status returned"):
            with patch.object(self.device, "adb_call", return_value=response):
                with self.assertRaisesRegex(RuntimeError, "not confirmed"):
                    self.device.inject_touch(100, 200, 80)

    def test_snapshot_single_use_and_after_observation(self):
        result = self.tap()
        self.assertTrue(result["input_sent"])
        self.assertNotEqual(result["after"]["snapshot_id"], self.snap["snapshot_id"])
        with self.assertRaisesRegex(ValueError, "already used"):
            self.tap()
        self.assertEqual(len(self.device.inputs), 1)

    def test_cross_client_lease_exclusion_and_release(self):
        other = FakeR1(Path(self.tmp.name))
        with self.assertRaisesRegex(ValueError, "leased"):
            other.lease("other chat")
        self.device.lease(token=self.token, release=True)
        other.lease("other chat")
        with self.assertRaises(ValueError):
            self.device.check_lease(self.token)

    def test_input_lock_excludes_another_client(self):
        other = FakeR1(Path(self.tmp.name))
        with self.device.input_lock():
            with self.assertRaisesRegex(ValueError, "in progress"):
                with other.input_lock():
                    self.fail("Concurrent input lock acquired")

    def test_bad_coordinates_duration_and_unknown_snapshot(self):
        with self.assertRaises(ValueError):
            self.device.tap(self.snap["snapshot_id"], self.token, x=480, y=300)
        with self.assertRaises(ValueError):
            self.device.swipe(self.snap["snapshot_id"], self.token, 10, 10, 20, 800)
        with self.assertRaises(ValueError):
            self.device.tap(self.snap["snapshot_id"], self.token, text="Музыка", hold_ms=9999)
        with self.assertRaises(ValueError):
            self.device.snapshot("../../private")
        self.assertEqual(self.device.inputs, [])

    def test_disabled_sonixlink_not_activated(self):
        self.assertFalse(self.device.now_playing()["available"])
        self.assertEqual(self.device.inputs, [])

    def test_typing_unknown_field_refused_without_input(self):
        with self.assertRaisesRegex(ValueError, "Search heading"):
            self.device.type_search(self.snap["snapshot_id"], self.token, "тест")
        self.assertEqual(self.device.inputs, [])

    def test_dimming_after_capture_refused(self):
        self.device.awake = False
        with self.assertRaisesRegex(ValueError, "unavailable"):
            self.tap()
        self.assertEqual(self.device.inputs, [])

    def test_ambiguous_label_refused(self):
        self.update(lines=self.snap["lines"] * 2)
        with self.assertRaisesRegex(ValueError, "Ambiguous"):
            self.tap()
        self.assertEqual(self.device.inputs, [])

    def keyboard(self):
        words = [{"text": ch, "x": 10 + i % 10 * 45, "y": 500 + i // 10 * 60,
                  "width": 20, "height": 24} for i, ch in enumerate("abcdefghijklmnopqrst")]
        self.update(lines=[{"text": "Поиск", "rect": [20, 70, 130, 95], "words": []},
                           {"text": "keyboard", "rect": [10, 500, 470, 650], "words": words}])

    def test_missing_key_checked_before_any_typing(self):
        self.keyboard()
        with self.assertRaisesRegex(ValueError, "absent"):
            self.device.type_search(self.snap["snapshot_id"], self.token, "az")
        self.assertEqual(self.device.inputs, [])

    def test_partial_typing_stops_when_keyboard_disappears(self):
        self.keyboard()
        result = self.device.type_search(self.snap["snapshot_id"], self.token, "ab")
        self.assertFalse(result["completed"])
        self.assertEqual(result["attempted"], "a")
        self.assertEqual(len(self.device.inputs), 1)

    def test_private_configuration_not_returned(self):
        probe = (b'/proc/sys/kernel/random/boot_id=45dfa894-fb85-4007-88d2-abb630624758\n'
                 b'/sys/class/graphics/fb0/blank=\n/sys/class/backlight/pwm/actual_brightness=38\n'
                 b'/sys/class/backlight/pwm/bl_power=0\n')
        config = b'[peq]\nenabled=1\npreset_name=Body\n[wireless]\nsonixlink=0\npassword=PRIVATE\n[yandex]\ntoken=SECRET\n'
        real = R1(Path(self.tmp.name))
        with patch.object(real, "adb_call", side_effect=[probe, config]):
            result = real.status()
        self.assertTrue(result["display_available"])
        self.assertNotIn("PRIVATE", json.dumps(result))
        self.assertNotIn("SECRET", json.dumps(result))
        self.assertEqual(result["settings"]["peq"]["preset_name"], "Body")


class DecodeTest(unittest.TestCase):
    def test_rgb565_endian_and_yoffset_metadata(self):
        header = b'{"width":480,"height":800,"format":"rgb565le","yoffset":800}\n'
        meta, image = decode_frame(header + b"\x00\xf8" + b"\xff\xff" * (480 * 800 - 1))
        self.assertEqual(image.getpixel((0, 0)), (255, 0, 0))
        self.assertEqual(image.getpixel((1, 0)), (255, 255, 255))
        self.assertEqual(meta["yoffset"], 800)

    def test_truncated_and_unknown_frames_rejected(self):
        with self.assertRaisesRegex(ValueError, "header"):
            decode_frame(b"")
        with self.assertRaises(ValueError):
            decode_frame(b'{"width":480,"height":800,"format":"rgb565le"}\nshort')
        with self.assertRaises(ValueError):
            decode_frame(b'{"width":800,"height":480,"format":"rgb565le"}\n')


if __name__ == "__main__":
    unittest.main()
