"""Regression checks for verified offline build and package input rejection.

The tiny synthetic lock/stage lets negative cases run without downloads, vendor
builds, or hardware. Real release archives are checked by test_package.py.
"""
import base64
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / file)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


FETCH = module('fetch_build_inputs', 'fetch-deps.py')
PACK = module('package_build_inputs', 'package.py')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pe32(payload=b'', machine=0x14c, timestamp=0):
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', data, 0x84, machine)
    struct.pack_into('<I', data, 0x88, timestamp)
    optional = 0x80 + 24
    struct.pack_into('<H', data, optional, 0x10b)
    struct.pack_into('<HH', data, optional+40, 5, 1)
    struct.pack_into('<HH', data, optional+48, 5, 1)
    struct.pack_into('<H', data, optional+68, 2)
    return bytes(data) + payload


class VerifiedBuildInputs(unittest.TestCase):
    def test_offline_missing_cache_never_downloads(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(FETCH, 'CACHE', Path(directory)), \
                patch.object(FETCH, 'download', side_effect=AssertionError('network attempted')):
            with self.assertRaisesRegex(SystemExit, 'offline'):
                FETCH.cached('missing', 'https://invalid', '00', offline=True)

    def test_offline_checks_hash_instead_of_trusting_cache(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(FETCH, 'CACHE', Path(directory)), \
                patch.object(FETCH, 'download', side_effect=AssertionError('network attempted')):
            path = Path(directory) / 'artifact'
            path.write_bytes(b'expected')
            self.assertEqual(FETCH.cached('artifact', 'https://invalid', digest(b'expected'), offline=True), path)
            path.write_bytes(b'tampered')
            with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
                FETCH.cached('artifact', 'https://invalid', digest(b'expected'), offline=True)

    def test_quickjs_rejects_host_x64_ucrt_and_nonreproducible_outputs(self):
        good_imports = 'DLL Name: KERNEL32.dll\nDLL Name: msvcrt.dll\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'qjs.exe'
            path.write_bytes(pe32())
            FETCH.validate_quickjs(path, good_imports)
            for content, imports in [(b'\x7fELF', good_imports),
                                     (pe32(machine=0x8664), good_imports),
                                     (pe32(timestamp=1), good_imports),
                                     (pe32(), 'DLL Name: KERNEL32.dll\nDLL Name: ucrtbase.dll'),
                                     (pe32(), good_imports + 'InitOnceExecuteOnce')]:
                with self.subTest(content=content[:4], imports=imports):
                    path.write_bytes(content)
                    with self.assertRaises(SystemExit):
                        FETCH.validate_quickjs(path, imports)


class ArchiveMetadata(unittest.TestCase):
    def test_shell_scripts_are_executable_and_metadata_is_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shell, readme = root / 'bootstrap.sh', root / 'README.md'
            shell.write_text('#!/bin/sh\necho ready\n'); readme.write_text('readme')
            files = [(shell, Path('source/scripts/bootstrap.sh')), (readme, Path('README.md'))]
            first, second = root / 'first.zip', root / 'second.zip'
            PACK.make_zip(first, files)
            shell.chmod(0o600); readme.chmod(0o755)
            PACK.make_zip(second, files)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertEqual((archive.getinfo('source/scripts/bootstrap.sh').external_attr >> 16) & 0o777, 0o755)
                self.assertEqual((archive.getinfo('README.md').external_attr >> 16) & 0o777, 0o644)


class PackageInputs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.stage, self.cache = self.root / 'stage', self.root / 'cache'
        self.stage.mkdir(); self.cache.mkdir()
        self.artifacts = {}
        self.manifest = {'artifacts': {}, 'quickjs_build': {}, 'ca_bundle': {}, 'vlc_nuget': {}}
        self.put('TubeLiviano.exe', pe32(b'frontend'))
        for flavor, name in [('modern', 'modern-yt-dlp_x86.exe'), ('xp', 'xp-yt-dlp_x86_winXP.exe')]:
            content = pe32(flavor.encode())
            self.put(f'engines/{flavor}/yt-dlp.exe', content)
            self.artifacts[name] = ('https://unused', digest(content))
            self.manifest['artifacts'][name] = {'sha256': digest(content)}
        qjs = pe32(b'quickjs')
        self.put('runtimes/qjs.exe', qjs)
        self.manifest['quickjs_build']['sha256'] = digest(qjs)
        ca, notice = b'certificate fixture', b'Certifi license fixture'
        self.put('vlc/certs/cacert.pem', ca)
        self.manifest['ca_bundle']['sha256'] = digest(ca)
        source = self.cache / 'certifi-test.tar.gz'
        with tarfile.open(source, 'w:gz') as archive:
            for name, content in [('certifi-test/certifi/cacert.pem', ca), ('certifi-test/LICENSE', notice)]:
                item = tarfile.TarInfo(name); item.size = len(content)
                archive.addfile(item, io.BytesIO(content))
        self.artifacts[source.name] = ('https://unused', digest(source.read_bytes()))
        for name in PACK.LICENSE_NAMES:
            content = notice if name == 'certifi-LICENSE' else name.encode()
            self.put('licenses/' + name, content)
            if name != 'certifi-LICENSE':
                self.artifacts[name] = ('https://unused', digest(content))
        package = self.cache / 'videolan.libvlc.windows.test.nupkg'
        self.vlc = {'libvlc.dll': pe32(b'vlc'), 'libvlccore.dll': pe32(b'vlc-core'),
                    'plugins/video_output/libwingdi_plugin.dll': pe32(b'wingdi')}
        with zipfile.ZipFile(package, 'w') as archive:
            for name, content in self.vlc.items():
                archive.writestr('build/x86/' + name, content)
                self.put('vlc/' + name, content)
        self.vlc_hash = base64.b64encode(hashlib.sha512(package.read_bytes()).digest()).decode()
        self.manifest['vlc_nuget']['sha512_base64'] = self.vlc_hash
        self.lock = SimpleNamespace(CERTIFI='test', VLC='test', VLC_HASH=self.vlc_hash, ARTIFACTS=self.artifacts)
        for height in (144, 240, 360):
            self.put(f'media/prueba-{height}.mp4', b'\0\0\0\x18ftypisom' + b'fixture')
        self.write_manifest()

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, relative, data):
        path = self.stage / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def write_manifest(self):
        self.put('licenses/dependencies.json', json.dumps(self.manifest).encode())

    def validate(self):
        return PACK.validate_stage(self.stage, slim=True, cache=self.cache, lock=self.lock)

    def test_valid_stage_and_unexpected_files_are_not_selected(self):
        self.put('selftest.json', b'old report')
        self.put('libvlccore.dll', pe32(b'wrong root DLL'))
        self.put('engines/xp/private.ini', b'not a runtime input')
        self.put('vlc/unknown.dll', pe32(b'not upstream'))
        _, files = self.validate()
        for relative in ('selftest.json', 'libvlccore.dll', 'engines/xp/private.ini', 'vlc/unknown.dll'):
            self.assertNotIn(relative, files)
        self.assertIn('media/prueba-144.mp4', files)
        self.assertIn('vlc/libvlccore.dll', files)

    def test_missing_offline_clip_is_a_preflight_error(self):
        (self.stage / 'media/prueba-240.mp4').unlink()
        with self.assertRaisesRegex(SystemExit, 'prueba-240'):
            self.validate()

    def test_same_architecture_wrong_or_swapped_engine_is_rejected(self):
        self.put('engines/modern/yt-dlp.exe', (self.stage / 'engines/xp/yt-dlp.exe').read_bytes())
        with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
            self.validate()

    def test_manifest_cannot_redefine_engine_source_pin(self):
        altered = pe32(b'not the release')
        self.put('engines/modern/yt-dlp.exe', altered)
        self.manifest['artifacts']['modern-yt-dlp_x86.exe']['sha256'] = digest(altered)
        self.write_manifest()
        with self.assertRaisesRegex(SystemExit, 'source pin'):
            self.validate()

    def test_wrong_vlc_core_even_if_manifest_updated_is_rejected(self):
        self.put('vlc/libvlccore.dll', pe32(b'wrong same-architecture VLC'))
        self.manifest['vlc_files'] = {'vlc/libvlccore.dll': digest(pe32(b'wrong same-architecture VLC'))}
        self.write_manifest()
        with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
            self.validate()

    def test_vlc_archive_and_ca_are_verified(self):
        with (self.cache / 'videolan.libvlc.windows.test.nupkg').open('ab') as file:
            file.write(b'corruption')
        with self.assertRaisesRegex(SystemExit, 'SHA512'):
            self.validate()
        self.put('vlc/certs/cacert.pem', b'wrong certificate')
        with self.assertRaisesRegex(SystemExit, 'Checksum mismatch'):
            self.validate()

    def test_missing_license_and_x64_frontend_are_rejected(self):
        path = self.stage / 'licenses/MPL-2.0.txt'
        content = path.read_bytes(); path.unlink()
        with self.assertRaisesRegex(SystemExit, 'MPL-2.0'):
            self.validate()
        path.write_bytes(content)
        self.put('TubeLiviano.exe', pe32(machine=0x8664))
        with self.assertRaisesRegex(SystemExit, 'PE32/i386'):
            self.validate()

    def test_missing_deterministic_qjs_cannot_be_hidden_by_manifest(self):
        wrong = pe32(timestamp=123)
        self.put('runtimes/qjs.exe', wrong)
        self.manifest['quickjs_build']['sha256'] = digest(wrong)
        self.write_manifest()
        with self.assertRaisesRegex(SystemExit, 'deterministic'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
