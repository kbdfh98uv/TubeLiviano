"""Bridge API and real FFmpeg streaming checks. Run: python3 -m unittest discover -s tests -p test_bridge.py -v"""
import http.client
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bridge_server", ROOT / "bridge" / "server.py")
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)


class BridgeValidation(unittest.TestCase):
    def test_only_youtube_sources(self):
        for url in ("https://www.youtube.com/watch?v=123", "https://youtu.be/123", "https://m.youtube.com/shorts/123"):
            self.assertTrue(bridge.youtube_url(url))
        for url in ("file:///etc/passwd", "http://127.0.0.1/x", "https://youtube.com.evil.example/x",
                    "https://youtube.com@localhost/x", "https://youtube.com:22/x", "https://youtube.com/\nx",
                    "https://youtube.com:bad/x", "https://user@youtube.com/watch?v=123"):
            self.assertFalse(bridge.youtube_url(url), url)

    def test_command_is_bounded_and_no_shell(self):
        for height in bridge.HEIGHTS:
            command = bridge.ffmpeg_command("ffmpeg", ["https://example.test/v", "https://example.test/a"], height)
            self.assertEqual(command.count("-i"), 2)
            self.assertIn("baseline", command)
            self.assertIn("yuv420p", command)
            self.assertIn(f"scale=-2:trunc(min({height}\\,ih)/2)*2,fps=24", command)
            self.assertIn("-re", command)
            self.assertEqual(command[-1], "pipe:1")
        with self.assertRaises(ValueError):
            bridge.ffmpeg_command("ffmpeg", ["source"], 720)

    def test_optional_javascript_runtime_is_one_argument(self):
        options = bridge.parser().parse_args([])
        self.assertNotIn("--js-runtimes", bridge.resolver_command(options, "https://youtu.be/test"))
        runtime = r"quickjs:C:\Tube Liviano\runtimes\qjs.exe"
        options = bridge.parser().parse_args(["--js-runtime", runtime])
        command = bridge.resolver_command(options, "https://youtu.be/test")
        self.assertEqual(command[command.index("--js-runtimes") + 1], runtime)
        self.assertEqual(command[-2:], ["--", "https://youtu.be/test"])


class RunningBridge:
    def __init__(self, file=None, token=""):
        options = bridge.parser().parse_args([])
        options.test_file = str(file) if file else None
        options.token = token
        self.server = bridge.BridgeServer(("127.0.0.1", 0), options)
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
        self.thread.start()

    def connection(self):
        return http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=15)

    def close(self):
        self.server.shutdown()
        self.server.stop_children()
        self.server.server_close()
        self.thread.join(timeout=2)


class BridgeHTTP(unittest.TestCase):
    def test_health_auth_and_bad_inputs(self):
        runner = RunningBridge(token="test-token")
        self.addCleanup(runner.close)
        connection = runner.connection()
        self.addCleanup(connection.close)
        connection.request("GET", "/health")
        response = connection.getresponse()
        self.assertEqual(response.status, 401)
        response.read()
        connection.request("GET", "/health?token=test-token")
        response = connection.getresponse()
        self.assertEqual(response.status, 200)
        self.assertEqual(json.loads(response.read())["heights"], [144, 240, 360])
        connection.request("GET", "/play?token=test-token&url=http://127.0.0.1&height=240")
        response = connection.getresponse()
        self.assertEqual(response.status, 400)
        response.read()
        connection.request("GET", "/play?token=test-token&url=https://youtu.be/test&height=720")
        response = connection.getresponse()
        self.assertEqual(response.status, 400)
        response.read()


@unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg/ffprobe requeridos")
class BridgeStreaming(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="tubeliviano-bridge-")
        cls.clip = Path(cls.directory.name) / "local.mp4"
        cls.long_clip = Path(cls.directory.name) / "long.mp4"
        base = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                "testsrc2=size=640x360:rate=30", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100",
                "-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p", "-c:a", "aac"]
        subprocess.run(base + ["-t", "3", "-movflags", "+faststart", str(cls.clip)], check=True, timeout=30)
        # A 96 s source makes a real early-output versus complete-file distinction.
        subprocess.run(base + ["-t", "96", "-movflags", "+faststart", str(cls.long_clip)], check=True, timeout=45)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_all_profiles_have_real_h264_audio_24fps(self):
        runner = RunningBridge(self.clip)
        self.addCleanup(runner.close)
        for height in bridge.HEIGHTS:
            with self.subTest(height=height):
                connection = runner.connection()
                try:
                    connection.request("GET", f"/play?url=https://youtu.be/offline&height={height}")
                    response = connection.getresponse()
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.getheader("Content-Type"), "video/mp2t")
                    data = response.read()
                finally:
                    connection.close()
                self.assertGreater(len(data), 188 * 10)
                target = Path(self.directory.name) / f"result-{height}.ts"
                target.write_bytes(data)
                result = subprocess.run(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(target)],
                                        capture_output=True, check=True, timeout=10)
                streams = json.loads(result.stdout)["streams"]
                video = next(s for s in streams if s["codec_type"] == "video")
                audio = next(s for s in streams if s["codec_type"] == "audio")
                self.assertEqual(video["codec_name"], "h264")
                self.assertIn("Baseline", video["profile"])
                self.assertEqual(video["height"], height)
                self.assertEqual(video["r_frame_rate"], "24/1")
                self.assertEqual(video["pix_fmt"], "yuv420p")
                self.assertEqual(audio["codec_name"], "aac")
                deadline = time.monotonic() + 3
                while runner.server.busy.locked() and time.monotonic() < deadline:
                    time.sleep(0.05)
                self.assertFalse(runner.server.busy.locked())

    def test_early_output_and_disconnect_kills_transcoder(self):
        runner = RunningBridge(self.long_clip)
        self.addCleanup(runner.close)
        connection = runner.connection()
        self.addCleanup(connection.close)
        started = time.monotonic()
        connection.request("GET", "/play?url=https://youtu.be/offline&height=240")
        response = connection.getresponse()
        data = response.read(188 * 4)
        elapsed = time.monotonic() - started
        self.assertEqual(response.status, 200)
        self.assertEqual(data[0], 0x47)  # MPEG-TS sync byte, actual output.
        self.assertLess(elapsed, 8, f"First data took {elapsed:.2f} s for a 96 s video")
        self.assertTrue(runner.server.busy.locked())
        self.assertTrue(runner.server.active)
        other = runner.connection()
        try:
            other.request("GET", "/play?url=https://youtu.be/offline&height=240")
            rejected = other.getresponse()
            self.assertEqual(rejected.status, 503)
            rejected.read()
        finally:
            other.close()
        response.close()
        connection.close()
        deadline = time.monotonic() + 6
        while (runner.server.busy.locked() or runner.server.active) and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertFalse(runner.server.busy.locked(), "Stream lock leaked on disconnect")
        self.assertFalse(runner.server.active, "FFmpeg remained alive after disconnect")


if __name__ == "__main__":
    unittest.main()
