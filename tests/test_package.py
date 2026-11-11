"""Inspect final portable archives; not a substitute for Windows runtime tests."""
import hashlib
import json
import pathlib
import posixpath
import re
import struct
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
PORTABLE = DIST / "TubeLiviano_Portable_0.5_x86.zip"
SOURCES = DIST / "TubeLiviano_ThirdParty_Sources_0.5.zip"
PREFIX = "TubeLiviano/"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def pe_header(data):
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise ValueError("Not a PE executable")
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if data[offset:offset + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    return machine, offset + 24


class PortableArchive(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not PORTABLE.is_file():
            raise unittest.SkipTest("Portable ZIP not built; package, then rerun test_package.py")
        cls.archive = zipfile.ZipFile(PORTABLE)
        cls.names = set(cls.archive.namelist())

    @classmethod
    def tearDownClass(cls):
        cls.archive.close()

    def read(self, relative):
        return self.archive.read(PREFIX + relative)

    def test_crc_paths_and_required_content(self):
        self.assertIsNone(self.archive.testzip())
        required = {
            "TubeLiviano.exe", "README.md", "LICENSE", "THIRD_PARTY.md",
            "vlc/libvlc.dll", "vlc/libvlccore.dll", "vlc/certs/cacert.pem",
            "engines/modern/yt-dlp.exe", "engines/xp/yt-dlp.exe", "runtimes/qjs.exe",
            "source/src/main.c", "source/src/player.c", "source/src/engine.c",
            "source/Makefile", "source/LICENSE", "source/docs/VALIDACION.md",
            "source/bridge/server.py", "source/bridge/README.md",
            "source/scripts/fetch-deps.py", "source/scripts/package.py",
            "licenses/dependencies.json", "licenses/vlc-COPYING", "licenses/vlc-COPYING.LIB",
            "licenses/quickjs-LICENSE", "licenses/yt-dlp-THIRD_PARTY_LICENSES.txt",
            "licenses/certifi-LICENSE", "licenses/MPL-2.0.txt",
        }
        required.update(f"media/prueba-{height}.mp4" for height in (144, 240, 360))
        self.assertFalse({PREFIX + name for name in required} - self.names)
        for name in self.names:
            with self.subTest(name=name):
                self.assertTrue(name.startswith(PREFIX))
                self.assertNotIn("..", pathlib.PurePosixPath(name).parts)
                self.assertNotIn("\\", name)
                self.assertNotIn("__pycache__", name)
                self.assertNotIn("validation-x64", name)

    def test_architecture_frontend_and_every_bundled_binary(self):
        binaries = [name for name in self.names if name.lower().endswith((".exe", ".dll"))]
        self.assertGreater(len(binaries), 20)
        for name in binaries:
            with self.subTest(name=name):
                data = self.archive.read(name)
                machine, optional = pe_header(data)
                self.assertEqual(machine, 0x14c, "An x64 or non-i386 binary entered the x86 ZIP")
                self.assertEqual(struct.unpack_from("<H", data, optional)[0], 0x10b)
        data = self.read("TubeLiviano.exe")
        _, optional = pe_header(data)
        self.assertEqual(struct.unpack_from("<HH", data, optional + 48), (5, 1))
        self.assertEqual(struct.unpack_from("<H", data, optional + 68)[0], 2)
        self.assertEqual(struct.unpack_from("<II", data, optional + 96 + 14 * 8), (0, 0))
        self.assertEqual(data, (ROOT / "build/portable/TubeLiviano.exe").read_bytes())

    def test_engine_identity_runtime_and_ca_hashes(self):
        manifest = json.loads(self.read("licenses/dependencies.json"))
        artifacts = manifest["artifacts"]
        modern = self.read("engines/modern/yt-dlp.exe")
        legacy = self.read("engines/xp/yt-dlp.exe")
        self.assertNotEqual(modern, legacy)
        self.assertEqual(sha256(modern), artifacts["modern-yt-dlp_x86.exe"]["sha256"])
        self.assertEqual(sha256(legacy), artifacts["xp-yt-dlp_x86_winXP.exe"]["sha256"])
        self.assertEqual(sha256(self.read("runtimes/qjs.exe")), manifest["quickjs_build"]["sha256"])
        certs = self.read("vlc/certs/cacert.pem")
        self.assertEqual(sha256(certs), manifest["ca_bundle"]["sha256"])
        self.assertEqual(certs.count(b"-----BEGIN CERTIFICATE-----"), manifest["ca_bundle"]["certificates"])

    def test_own_source_license_and_readme_links_are_current(self):
        self.assertIn(b"MIT License", self.read("LICENSE"))
        self.assertEqual(self.read("LICENSE"), self.read("source/LICENSE"))
        for relative in ("src/main.c", "src/player.c", "src/engine.c", "docs/VALIDACION.md"):
            with self.subTest(relative=relative):
                self.assertEqual(self.read("source/" + relative), (ROOT / relative).read_bytes())
        for readme in ("README.md", "source/README.md"):
            text = self.read(readme).decode("utf-8")
            for link in re.findall(r"\]\(([^)]+)\)", text):
                if "://" in link or link.startswith(("#", "mailto:")):
                    continue
                target = posixpath.normpath(posixpath.join(PREFIX, posixpath.dirname(readme), link.split("#", 1)[0]))
                with self.subTest(readme=readme, link=link):
                    self.assertIn(target, self.names)

    def test_source_companion_and_external_report(self):
        self.assertTrue(SOURCES.is_file(), "Third-party source companion is missing")
        report = json.loads((DIST / "package-report.json").read_text())
        self.assertEqual(report["portable"]["bytes"], PORTABLE.stat().st_size)
        self.assertEqual(report["portable"]["sha256"], sha256(PORTABLE.read_bytes()))
        self.assertEqual(report["upstream_sources"]["bytes"], SOURCES.stat().st_size)
        self.assertEqual(report["upstream_sources"]["sha256"], sha256(SOURCES.read_bytes()))
        with zipfile.ZipFile(SOURCES) as archive:
            self.assertIsNone(archive.testzip())
            expected = {"modern-yt-dlp.tar.gz", "xp-yt-dlp.tar.gz", "vlc-3.0.21-source.tar.gz",
                        "quickjs-0.18.0-source.tar.gz", "yt_dlp_ejs-0.8.0.tar.gz",
                        "certifi-2026.10.23.tar.gz"}
            self.assertTrue({"upstream/" + name for name in expected} <= set(archive.namelist()))
            self.assertIn("recipes/fetch-deps.py", archive.namelist())


if __name__ == "__main__":
    unittest.main(verbosity=2)
