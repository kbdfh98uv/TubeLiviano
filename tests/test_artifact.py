"""Static checks on the actual deliverable, independent of Wine availability."""
import pathlib
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STAGE = ROOT / "build/portable"


def pe(path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("Not a PE")
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if data[offset:offset+4] != b"PE\0\0":
        raise ValueError("Not a PE")
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    opt = offset + 24
    return data, opt, machine


class Artifact(unittest.TestCase):
    def test_native_x86_xp_subsystem_and_size(self):
        data, opt, machine = pe(STAGE / "TubeLiviano.exe")
        self.assertEqual(machine, 0x14c)
        self.assertEqual(struct.unpack_from("<H", data, opt)[0], 0x10b)
        self.assertEqual(struct.unpack_from("<HH", data, opt + 48), (5, 1))
        self.assertEqual(struct.unpack_from("<H", data, opt + 68)[0], 2)  # GUI
        self.assertEqual(struct.unpack_from("<II", data, opt + 96 + 14*8), (0, 0))  # no CLR
        self.assertLess(len(data), 100_000, "Frontend no longer tens of KB")

    def test_both_engines_and_runtime_are_x86(self):
        for relative in ("engines/xp/yt-dlp.exe", "engines/modern/yt-dlp.exe",
                         "runtimes/qjs.exe", "vlc/libvlc.dll", "vlc/libvlccore.dll"):
            with self.subTest(relative=relative):
                _, _, machine = pe(STAGE / relative)
                self.assertEqual(machine, 0x14c)
        self.assertNotEqual((STAGE / "engines/xp/yt-dlp.exe").read_bytes(),
                            (STAGE / "engines/modern/yt-dlp.exe").read_bytes())

    def test_offline_fixtures_certs_and_provenance(self):
        for h in (144, 240, 360):
            self.assertGreater((STAGE / f"media/prueba-{h}.mp4").stat().st_size, 5000)
        self.assertIn(b"BEGIN CERTIFICATE", (STAGE / "vlc/certs/cacert.pem").read_bytes())
        self.assertTrue((STAGE / "licenses/dependencies.json").is_file())


if __name__ == "__main__":
    unittest.main()
