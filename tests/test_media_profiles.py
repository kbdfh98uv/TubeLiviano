"""Verify the actual offline artifacts independently of the generator."""
import fractions
import json
import pathlib
import shutil
import struct
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MEDIA = ROOT / "build/portable/media"


class OfflineMedia(unittest.TestCase):
    @unittest.skipUnless(shutil.which("ffprobe"), "ffprobe unavailable; media profiles NOT EXECUTED")
    def test_three_codec_profiles(self):
        for height in (144, 240, 360):
            with self.subTest(height=height):
                result = subprocess.run([
                    "ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json",
                    str(MEDIA / f"prueba-{height}.mp4"),
                ], capture_output=True, text=True, check=True, timeout=20)
                metadata = json.loads(result.stdout)
                video = next(s for s in metadata["streams"] if s["codec_type"] == "video")
                audio = next(s for s in metadata["streams"] if s["codec_type"] == "audio")
                self.assertEqual(video["codec_name"], "h264")
                self.assertIn(video["profile"], ("Baseline", "Constrained Baseline"))
                self.assertEqual(video["height"], height)
                self.assertEqual(video["pix_fmt"], "yuv420p")
                self.assertEqual(fractions.Fraction(video["avg_frame_rate"]), 24)
                self.assertEqual(audio["codec_name"], "aac")
                self.assertEqual(audio["channels"], 1)
                self.assertEqual(int(audio["sample_rate"]), 44100)
                self.assertAlmostEqual(float(metadata["format"]["duration"]), 8, delta=.2)

    def test_indexes_before_media(self):
        for height in (144, 240, 360):
            with self.subTest(height=height):
                data = (MEDIA / f"prueba-{height}.mp4").read_bytes()
                atoms = []
                offset = 0
                while offset + 8 <= len(data):
                    size, kind = struct.unpack_from(">I4s", data, offset)
                    if size == 1:
                        size = struct.unpack_from(">Q", data, offset + 8)[0]
                    elif size == 0:
                        size = len(data) - offset
                    self.assertGreaterEqual(size, 8)
                    self.assertLessEqual(offset + size, len(data))
                    atoms.append(kind)
                    offset += size
                self.assertEqual(offset, len(data))
                self.assertLess(atoms.index(b"moov"), atoms.index(b"mdat"), "Index moved to end; early playback regressed")


if __name__ == "__main__":
    unittest.main()
