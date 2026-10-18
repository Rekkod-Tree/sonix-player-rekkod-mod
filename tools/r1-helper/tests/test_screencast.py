from array import array
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from screencast import decode, export_frames


class RecordingTest(unittest.TestCase):
    def recording(self, tokens):
        words = array("H", tokens)
        if sys.byteorder != "little": words.byteswap()
        payload = words.tobytes()
        return b"SNXR1V1\n" + struct.pack("<II", 480, 800) + struct.pack("<II", 1, len(payload)) + payload

    def test_rgb565_and_unchanged_runs(self):
        # Red first pixel, every other pixel unchanged from initial black.
        words = [0xc000, 0xf800]
        remaining = 480 * 800 - 1
        while remaining:
            count = min(32768, remaining)
            words.append(count - 1)
            remaining -= count
        _, image = next(decode(self.recording(words)))
        self.assertEqual(image.getpixel((0, 0)), (255, 0, 0))
        self.assertEqual(image.getpixel((1, 0)), (0, 0, 0))

    def test_truncation_and_incomplete_frame_rejected(self):
        with self.assertRaises(ValueError): list(decode(self.recording([0xc000])))
        with self.assertRaises(ValueError): list(decode(self.recording([0])))

    def test_missing_or_truncated_recording_header_rejected(self):
        with self.assertRaises(ValueError): list(decode(b"SNXR1V1\n"))
        with self.assertRaises(ValueError): list(decode(b""))

    def test_export_preserves_real_frame_intervals(self):
        words, remaining = [], 480 * 800
        while remaining:
            count = min(32768, remaining)
            words.append(count - 1)
            remaining -= count
        data = self.recording(words)
        payload = data[24:]
        data += struct.pack("<II", 126, len(payload)) + payload
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            self.assertEqual(export_frames(data, folder), (2, 1, 126))
            self.assertIn("duration 0.125000", (folder / "frames.ffconcat").read_text())
            self.assertEqual(len(list(folder.glob("*.png"))), 2)


if __name__ == "__main__": unittest.main()
