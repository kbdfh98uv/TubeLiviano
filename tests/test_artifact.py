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


def pe_imports(data):
    """Read PE32 imports directly; independent of objdump's output format."""
    header = struct.unpack_from('<I', data, 0x3c)[0]
    optional = header + 24
    count = struct.unpack_from('<H', data, header + 6)[0]
    sections = optional + struct.unpack_from('<H', data, header + 20)[0]

    def offset(rva):
        for index in range(count):
            virtual_size, address, size, raw = struct.unpack_from('<IIII', data, sections + index * 40 + 8)
            if address <= rva < address + max(virtual_size, size):
                result = raw + rva - address
                if result >= len(data):
                    break
                return result
        raise ValueError('Import RVA outside file sections')

    def string(rva):
        begin = offset(rva)
        end = data.index(b'\0', begin, min(begin + 1024, len(data)))
        return data[begin:end].decode('ascii')

    directory = offset(struct.unpack_from('<I', data, optional + 104)[0])
    result = {}
    for index in range(128):
        lookup, stamp, chain, name, first = struct.unpack_from('<IIIII', data, directory + index * 20)
        if not any((lookup, stamp, chain, name, first)):
            return result
        functions = []
        table = offset(lookup or first)
        for item in range(4096):
            entry = struct.unpack_from('<I', data, table + item * 4)[0]
            if not entry:
                break
            functions.append('#%d' % (entry & 0xffff) if entry & 0x80000000 else string(entry + 2))
        else:
            raise ValueError('Unterminated import name table')
        result[string(name)] = functions
    raise ValueError('Unterminated import directory')


class Artifact(unittest.TestCase):
    def test_native_x86_xp_subsystem_and_size(self):
        data, opt, machine = pe(STAGE / "TubeLiviano.exe")
        self.assertEqual(machine, 0x14c)
        self.assertEqual(struct.unpack_from("<H", data, opt)[0], 0x10b)
        self.assertEqual(struct.unpack_from("<HH", data, opt + 48), (5, 1))
        self.assertEqual(struct.unpack_from("<H", data, opt + 68)[0], 2)  # GUI
        self.assertEqual(struct.unpack_from("<II", data, opt + 96 + 14*8), (0, 0))  # no CLR
        self.assertLess(len(data), 100 * 1024, "Frontend exceeded the 100 KiB budget")

    def test_frontend_system_imports_and_legacy_apis(self):
        imports = pe_imports((STAGE / 'TubeLiviano.exe').read_bytes())
        self.assertEqual({name.upper() for name in imports},
                         {'GDI32.DLL', 'KERNEL32.DLL', 'MSVCRT.DLL', 'SHELL32.DLL', 'USER32.DLL'})
        post_xp = {'GetTickCount64', 'InitializeCriticalSectionEx', 'SetDefaultDllDirectories',
                   'AddDllDirectory', 'GetSystemTimePreciseAsFileTime', 'CreateFile2',
                   'GetFileInformationByHandleEx', 'CancelSynchronousIo',
                   'InitializeConditionVariable', 'SleepConditionVariableCS'}
        for library, functions in imports.items():
            with self.subTest(library=library):
                self.assertTrue(functions, 'Empty import parsing must never pass the API audit')
                self.assertFalse(any(name.startswith('#') for name in functions), 'Named imports required for API audit')
                self.assertFalse(set(functions) & post_xp)

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
