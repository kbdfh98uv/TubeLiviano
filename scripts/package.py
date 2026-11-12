#!/usr/bin/env python3
"""Build verified portable/source ZIPs from a prepared native Windows stage.

All runtime inputs and upstream sources are checked before an archive is written.
No downloads occur here. Unexpected stage files are reported and never packaged.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import struct
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.5.2'

# This exact reduced profile must be used by the offline playback validation.
SLIM_PLUGINS = {
    'access': ['filesystem', 'http', 'https', 'tcp', 'udp', 'imem', 'access_imem'],
    'audio_filter': ['audio_format', 'trivial_channel_mixer', 'simple_channel_mixer',
                     'ugly_resampler', 'speex_resampler', 'scaletempo'],
    'audio_output': ['waveout', 'directsound', 'mmdevice', 'wasapi', 'adummy', 'amem'],
    'codec': ['avcodec', 'araw', 'adpcm', 'mpg123', 'rawvideo', 'ddummy'],
    'demux': ['mp4', 'ts', 'es', 'h26x', 'wav', 'rawaud', 'rawvid', 'avi', 'mkv'],
    'misc': ['gnutls', 'xml'],
    'packetizer': ['packetizer_h264', 'packetizer_mpeg4audio', 'packetizer_mpegaudio',
                   'packetizer_copy'],
    'stream_filter': ['cache_read', 'prefetch', 'record'],
    'video_output': ['wingdi', 'directdraw', 'direct3d9', 'drawable', 'vdummy',
                     'vmem', 'winhibit'],
}
LICENSE_NAMES = ['vlc-COPYING', 'vlc-COPYING.LIB', 'yt-dlp-LICENSE',
                 'quickjs-LICENSE', 'yt-dlp-THIRD_PARTY_LICENSES.txt',
                 'certifi-LICENSE', 'MPL-2.0.txt']


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependency_lock():
    spec = importlib.util.spec_from_file_location('tubeliviano_dependency_lock', ROOT / 'scripts/fetch-deps.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pe_info(path):
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise SystemExit(f'{path} is not a Windows PE executable/DLL.')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    optional = offset + 24
    if (optional + 216 > len(data) or data[offset:offset+4] != b'PE\0\0' or
            struct.unpack_from('<H', data, offset+4)[0] != 0x14c or
            struct.unpack_from('<H', data, optional)[0] != 0x10b):
        raise SystemExit(f'{path} is not PE32/i386.')
    return data, offset, optional


def is_pe32(path):
    try:
        pe_info(path)
        return True
    except SystemExit:
        return False


def slim_vlc_path(relative):
    rel = Path(relative)
    if rel.parts[:2] == ('vlc', 'plugins'):
        if len(rel.parts) != 4:
            return False
        family, name = rel.parts[2:]
        return family == 'video_chroma' or name in {
            f'lib{x}_plugin.dll' for x in SLIM_PLUGINS.get(family, [])}
    return rel.parts[:2] not in [('vlc', 'lua'), ('vlc', 'hrtfs')]


def verified_file(stage, relative):
    path = stage / relative
    if not path.is_file() or not path.resolve().is_relative_to(stage.resolve()):
        raise SystemExit(f'Missing or external runtime file: {path}')
    return path


def expect_hash(path, digest):
    if sha256(path) != digest:
        raise SystemExit(f'Checksum mismatch: {path}')


def verified_source(cache, name, lock):
    path = cache / name
    if not path.is_file():
        raise SystemExit(f'Missing upstream cache: {path}; run fetch-deps.py --sources.')
    expect_hash(path, lock.ARTIFACTS[name][1])
    return path


def validate_stage(stage, slim=False, cache=None, lock=None):
    """Return the exact runtime file set after preflight, without mutations."""
    cache = cache or ROOT / '.cache'
    lock = lock or dependency_lock()
    manifest_path = verified_file(stage, 'licenses/dependencies.json')
    try:
        manifest = json.loads(manifest_path.read_text())
    except (ValueError, OSError) as exc:
        raise SystemExit(f'Invalid dependency manifest: {exc}')
    runtime = {'TubeLiviano.exe', 'runtimes/qjs.exe', 'vlc/certs/cacert.pem',
               'licenses/dependencies.json'}
    for flavor, name in [('modern', 'modern-yt-dlp_x86.exe'), ('xp', 'xp-yt-dlp_x86_winXP.exe')]:
        relative = f'engines/{flavor}/yt-dlp.exe'
        path = verified_file(stage, relative)
        digest = lock.ARTIFACTS[name][1]
        if manifest.get('artifacts', {}).get(name, {}).get('sha256') != digest:
            raise SystemExit(f'Engine manifest differs from the source pin: {name}')
        expect_hash(path, digest)
        runtime.add(relative)
    try:
        expect_hash(verified_file(stage, 'runtimes/qjs.exe'), manifest['quickjs_build']['sha256'])
    except KeyError as exc:
        raise SystemExit(f'Missing QuickJS provenance: {exc}')
    qjs, pe, optional = pe_info(stage / 'runtimes/qjs.exe')
    if (struct.unpack_from('<I', qjs, pe+8)[0] != 0 or
            struct.unpack_from('<HH', qjs, optional+40) != (5, 1) or
            struct.unpack_from('<HH', qjs, optional+48) != (5, 1)):
        raise SystemExit('QuickJS is not the deterministic XP 5.01 build; refresh dependencies.')
    cert_source = verified_source(cache, f'certifi-{lock.CERTIFI}.tar.gz', lock)
    with tarfile.open(cert_source) as archive:
        ca = archive.extractfile(f'certifi-{lock.CERTIFI}/certifi/cacert.pem').read()
        ca_notice = archive.extractfile(f'certifi-{lock.CERTIFI}/LICENSE').read()
    ca_hash = hashlib.sha256(ca).hexdigest()
    if manifest.get('ca_bundle', {}).get('sha256') != ca_hash:
        raise SystemExit('CA manifest differs from the verified Certifi source.')
    expect_hash(verified_file(stage, 'vlc/certs/cacert.pem'), ca_hash)
    for name in LICENSE_NAMES:
        relative = f'licenses/{name}'
        path = verified_file(stage, relative)
        if name == 'certifi-LICENSE':
            if path.read_bytes() != ca_notice:
                raise SystemExit('Certifi license differs from the verified source.')
        else:
            expect_hash(path, lock.ARTIFACTS[name][1])
        runtime.add(relative)
    package = cache / f'videolan.libvlc.windows.{lock.VLC}.nupkg'
    if not package.is_file():
        raise SystemExit(f'Missing verified VLC package cache: {package}')
    if base64.b64encode(hashlib.sha512(package.read_bytes()).digest()).decode() != lock.VLC_HASH:
        raise SystemExit('VLC NuGet cache differs from the pinned SHA512.')
    if manifest.get('vlc_nuget', {}).get('sha512_base64') != lock.VLC_HASH:
        raise SystemExit('VLC manifest differs from the source pin.')
    with zipfile.ZipFile(package) as archive:
        for item in archive.infolist():
            if item.is_dir() or not item.filename.startswith('build/x86/'):
                continue
            rel = Path(item.filename[len('build/x86/'):])
            if rel.is_absolute() or '..' in rel.parts:
                raise SystemExit('Unsafe VLC archive path.')
            if rel.parts[0] == 'include' or rel.suffix == '.lib':
                continue
            relative = 'vlc/' + rel.as_posix()
            if slim and not slim_vlc_path(relative):
                continue
            expect_hash(verified_file(stage, relative), hashlib.sha256(archive.read(item)).hexdigest())
            runtime.add(relative)
    for height in (144, 240, 360):
        relative = f'media/prueba-{height}.mp4'
        path = verified_file(stage, relative)
        with path.open('rb') as stream:
            if stream.read(12)[4:8] != b'ftyp':
                raise SystemExit(f'Invalid offline MP4 fixture: {path}')
        runtime.add(relative)
    for relative in sorted(runtime):
        path = verified_file(stage, relative)
        if path.suffix.lower() in ('.exe', '.dll'):
            pe_info(path)
    frontend, _, optional = pe_info(stage / 'TubeLiviano.exe')
    if (struct.unpack_from('<HH', frontend, optional+48) != (5, 1) or
            struct.unpack_from('<II', frontend, optional+96+14*8) != (0, 0)):
        raise SystemExit('Frontend must be native XP subsystem 5.01, without CLR.')
    return manifest, runtime


def source_files():
    for name in ['src', 'scripts', 'bridge', 'tests', 'docs']:
        folder = ROOT / name
        if folder.exists():
            for file in sorted(folder.rglob('*')):
                if file.is_file() and '__pycache__' not in file.parts and file.suffix not in {'.pyc', '.exe', '.dll', '.o'}:
                    yield file
    for name in ['README.md', 'LICENSE', 'THIRD_PARTY.md', 'Makefile', 'CMakeLists.txt', '.gitignore']:
        file = ROOT / name
        if file.is_file():
            yield file


def make_zip(path, files):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for source, target in files:
            info = zipfile.ZipInfo(str(target).replace('\\', '/'), (2026, 11, 10, 0, 0, 0))
            info.external_attr = (0o755 if source.suffix == '.sh' else 0o644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, source.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package(stage, output, slim):
    lock = dependency_lock()
    manifest, runtime = validate_stage(stage, slim, lock=lock)
    vendor_sources = []
    for name in ['modern-yt-dlp.tar.gz', 'xp-yt-dlp.tar.gz', f'vlc-{lock.VLC}-source.tar.gz',
                 f'quickjs-{lock.QJS}-source.tar.gz', 'yt_dlp_ejs-0.8.0.tar.gz',
                 f'certifi-{lock.CERTIFI}.tar.gz']:
        vendor_sources.append((verified_source(ROOT / '.cache', name, lock), Path('upstream') / name))
    sources = list(source_files())
    if not (ROOT / 'README.md').is_file() or not (ROOT / 'LICENSE').is_file():
        raise SystemExit('README and MIT license are required.')
    source_dir = stage / 'source'
    if source_dir.exists():
        shutil.rmtree(source_dir)
    for file in sources:
        destination = source_dir / file.relative_to(ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, destination)
    generated = ['README.md', 'LICENSE', 'THIRD_PARTY.md']
    for name in generated:
        shutil.copy2(ROOT / name, stage / name)
    batch = ROOT / 'scripts/probar-offline.cmd'
    if not batch.is_file():
        raise SystemExit('Missing scripts/probar-offline.cmd; the portable offline check is required.')
    # XP cmd.exe receives ASCII/CRLF commands regardless of the build host.
    batch_bytes = batch.read_bytes().replace(b'\r\n', b'\n').replace(b'\r', b'\n')
    if batch_bytes.startswith(b'\xef\xbb\xbf') or not batch_bytes.isascii():
        raise SystemExit('The XP batch check must be ASCII without a BOM.')
    (stage / 'Probar_offline.cmd').write_bytes(batch_bytes.replace(b'\n', b'\r\n'))
    generated.append('Probar_offline.cmd')
    readme = (stage / 'README.md').read_text()
    readme = readme.replace('](docs/', '](source/docs/').replace('](bridge/', '](source/bridge/')
    readme = readme.replace('`docs/VALIDACION.md`', '`source/docs/VALIDACION.md`')
    readme = readme.replace('## Compilar desde Linux\n',
        '## Compilar desde Linux\n\nLos comandos siguientes se ejecutan desde `source/` '
        'en el ZIP, o desde la raíz del repositorio.\n')
    (stage / 'README.md').write_text(readme)
    for readme_path in [stage / 'README.md', source_dir / 'README.md']:
        for link in re.findall(r'\]\(([^)]+)\)', readme_path.read_text()):
            if '://' in link or link.startswith(('#', 'mailto:')):
                continue
            if not (readme_path.parent / link.split('#', 1)[0]).is_file():
                raise SystemExit(f'Broken packaged README link: {readme_path} -> {link}')
    accepted = runtime | set(generated)
    files = [(stage / relative, Path('TubeLiviano') / relative) for relative in sorted(accepted)]
    files += [(file, Path('TubeLiviano/source') / file.relative_to(source_dir))
              for file in sorted(source_dir.rglob('*')) if file.is_file()]
    included = {str(target.relative_to('TubeLiviano')) for _, target in files}
    omitted = [str(file.relative_to(stage)) for file in sorted(stage.rglob('*'))
               if file.is_file() and str(file.relative_to(stage)) not in included]
    vendor_sources += [(ROOT / 'THIRD_PARTY.md', Path('THIRD_PARTY.md')),
                       (ROOT / 'scripts/fetch-deps.py', Path('recipes/fetch-deps.py')),
                       (stage / 'licenses/dependencies.json', Path('dependencies.json'))]
    output.mkdir(parents=True, exist_ok=True)
    name = f'TubeLiviano_Portable_{VERSION}_x86.zip'
    source_name = f'TubeLiviano_ThirdParty_Sources_{VERSION}.zip'
    portable_tmp, source_tmp = output / (name + '.tmp'), output / (source_name + '.tmp')
    make_zip(portable_tmp, files)
    make_zip(source_tmp, vendor_sources)
    portable_tmp.replace(output / name)
    source_tmp.replace(output / source_name)
    report = {'version': VERSION, 'vlc_profile': 'slim' if slim else 'full',
              'portable': {'file': name, 'bytes': (output / name).stat().st_size,
                           'sha256': sha256(output / name)},
              'upstream_sources': {'file': source_name, 'bytes': (output / source_name).stat().st_size,
                                   'sha256': sha256(output / source_name)},
              'files': len(files), 'omitted_vlc_files': [p for p in omitted if p.startswith('vlc/')],
              'excluded_stage_files': omitted}
    report_tmp = output / 'package-report.json.tmp'
    report_tmp.write_text(json.dumps(report, indent=2) + '\n')
    report_tmp.replace(output / 'package-report.json')
    for item in [report['portable'], report['upstream_sources']]:
        print(f"{item['file']}: {item['bytes'] / 1_000_000:.2f} MB; SHA256 {item['sha256']}")
    print('VLC profile:', report['vlc_profile'], '; runtime tests must use the same profile.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=ROOT / 'build' / 'portable')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--slim-vlc', action='store_true', help='only after testing the reduced plugin profile')
    args = parser.parse_args()
    package(args.stage.resolve(), args.output.resolve(), args.slim_vlc)
