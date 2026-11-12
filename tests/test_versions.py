"""Catch a release with mismatched window, resource, manifest or ZIP versions."""
import ast
import pathlib
import re
import struct
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]


def release_version():
    tree = ast.parse((ROOT / "scripts/package.py").read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "VERSION" for t in node.targets):
            return ast.literal_eval(node.value)
    raise AssertionError("Package version missing")


class Versions(unittest.TestCase):
    def test_window_manifest_resource_and_package_agree(self):
        version = release_version()
        parts = tuple(int(s) for s in version.split("."))
        self.assertEqual(len(parts), 3)
        main = (ROOT / "src/main.c").read_text()
        self.assertEqual(re.search(r'^#define VERSION L"([^"]+)"', main, re.M).group(1), version)
        identity = ET.parse(ROOT / "src/app.manifest").getroot().find("{urn:schemas-microsoft-com:asm.v1}assemblyIdentity")
        self.assertEqual(identity.attrib["version"], version + ".0")
        resource = (ROOT / "src/app.rc").read_text()
        for field in ("FILEVERSION", "PRODUCTVERSION"):
            value = re.search(r"^" + field + r" ([0-9,]+)", resource, re.M).group(1)
            self.assertEqual(tuple(map(int, value.split(","))), parts + (0,))
        for field in ("FileVersion", "ProductVersion"):
            self.assertIn(f'VALUE "{field}", "{version}\\0"', resource)

    def test_compiled_fixed_file_info_matches_source(self):
        data = (ROOT / "build/portable/TubeLiviano.exe").read_bytes()
        signature = struct.pack("<II", 0xFEEF04BD, 0x10000)
        at = data.find(signature)
        self.assertGreaterEqual(at, 0, "VERSIONINFO resource missing")
        file_ms, file_ls, product_ms, product_ls = struct.unpack_from("<IIII", data, at + 8)
        a, b, c = map(int, release_version().split("."))
        self.assertEqual((file_ms, file_ls), ((a << 16) | b, c << 16))
        self.assertEqual((product_ms, product_ls), (file_ms, file_ls))


if __name__ == "__main__":
    unittest.main()
