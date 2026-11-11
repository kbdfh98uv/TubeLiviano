#!/usr/bin/env python3
"""Exercise the actual x86 app + bundled libVLC under a working Wine/X server.

Usage: WINE=... DISPLAY=:99 WINEPREFIX=... python3 tests/playback.py
Writes current-run JSON reports, verifies decoded video+audio, and uses a local
HTTP Range origin to measure bytes served at first displayed frame. No YouTube.
"""
import http.server
import json
import os
import pathlib
import re
import subprocess
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PORTABLE = pathlib.Path(os.environ.get("TL_TEST_STAGE", str(ROOT / "build/portable")))
REPORTS = ROOT / "build/reports"
REPORTS.mkdir(parents=True, exist_ok=True)


class Origin(http.server.BaseHTTPRequestHandler):
    data = b""
    sent = 0
    ranges = []
    first_byte_at = None

    def log_message(self, *args):
        pass

    def do_GET(self):
        cls = type(self)
        start, end = 0, len(cls.data) - 1
        r = self.headers.get("Range")
        cls.ranges.append(r)
        if r:
            match = re.fullmatch(r"bytes=(\d+)-(\d*)", r)
            if not match:
                self.send_error(416)
                return
            start = int(match[1])
            if match[2]:
                end = min(end, int(match[2]))
        if start > end:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{len(cls.data)}")
            self.end_headers()
            return
        self.send_response(206 if r else 200)
        self.send_header("Content-Type", "video/mp4")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if r:
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(cls.data)}")
        self.end_headers()
        try:
            for offset in range(start, end + 1, 2048):
                chunk = cls.data[offset:min(offset + 2048, end + 1)]
                self.wfile.write(chunk)
                self.wfile.flush()
                cls.sent += len(chunk)
                if cls.first_byte_at is None:
                    cls.first_byte_at = time.monotonic()
                # 64 KiB/s: realistic slow link, makes full-download regressions visible.
                time.sleep(len(chunk) / 65536)
        except (BrokenPipeError, ConnectionResetError):
            pass


class Playback(unittest.TestCase):
    def launch(self, media, label, milliseconds=5500):
        report = REPORTS / (label + ".json")
        report.unlink(missing_ok=True)
        win = lambda p: "Z:" + str(p).replace("/", "\\")
        cmd = [os.environ.get("WINE", "wine"), str(PORTABLE / "TubeLiviano.exe"),
               "--self-test-media", media, "--report", win(report), "--test-ms", str(milliseconds)]
        with (REPORTS / (label + ".log")).open("w") as output:
            result = subprocess.run(cmd, stdout=output, stderr=output, timeout=70)
        self.assertTrue(report.exists(), f"App did not produce {report}; see .log")
        data = json.loads(report.read_text())
        self.assertEqual(result.returncode, 0, data)
        self.assertTrue(data["complete"], data)
        self.assertTrue(data["passed"], data)
        for key in ("decoded_video", "decoded_audio", "displayed_pictures"):
            self.assertGreater(data[key], 0, data)
        return data

    def test_local_profiles(self):
        for height in (144, 240, 360):
            with self.subTest(height=height):
                path = PORTABLE / f"media/prueba-{height}.mp4"
                self.launch("Z:" + str(path).replace("/", "\\"), f"local-{height}")

    def test_range_before_full_file(self):
        Origin.data = (ROOT / "build/test-media/range-96s.mp4").read_bytes()
        Origin.sent, Origin.ranges, Origin.first_byte_at = 0, [], None
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Origin)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            data = self.launch(f"http://127.0.0.1:{server.server_port}/clip.mp4", "range-96s", 4500)
            data["origin_sent_bytes"] = Origin.sent
            data["file_bytes"] = len(Origin.data)
            data["range_requests"] = Origin.ranges
            data["first_frame_fraction"] = data["first_frame_read_bytes"] / len(Origin.data)
            (REPORTS / "range-measurement.json").write_text(json.dumps(data, indent=2))
            self.assertTrue(any(r is not None for r in Origin.ranges), "No HTTP Range request")
            self.assertLess(Origin.sent, len(Origin.data), "Downloaded all before playback finished")
            self.assertGreater(data["first_frame_read_bytes"], 0)
            self.assertLess(data["first_frame_fraction"], .25, data)
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
