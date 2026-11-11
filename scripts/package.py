#!/usr/bin/env python3
"""Build the portable ZIP and a separate upstream-source companion archive.

Run after fetch-deps.py --sources, the native build, and runtime smoke tests.
No downloads occur here. The stage must already contain working dependencies.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.5'

# Minimal multimedia profile: native HTTP/TLS, MP4/MPEG-TS, H264/AAC/MP3,
# software chroma conversion, waveOut/DirectSound and GDI/DirectX video output.
# Keep the normal/full profile unless this exact reduced profile was smoke-tested.
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


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def is_pe32(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        return False
    offset = int.from_bytes(data[0x3c:0x40], 'little')
    return data[offset:offset+6] == b'PE\0\0\x4c\x01'


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


def make_zip(path: Path, files):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for source, target in files:
            info = zipfile.ZipInfo(str(target).replace('\\', '/'), (2026, 11, 10, 0, 0, 0))
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, source.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def package(stage: Path, output: Path, slim: bool):
    required = ['TubeLiviano.exe', 'vlc/libvlc.dll', 'vlc/libvlccore.dll',
                'engines/modern/yt-dlp.exe', 'engines/xp/yt-dlp.exe',
                'runtimes/qjs.exe',
                'vlc/certs/cacert.pem', 'licenses/certifi-LICENSE', 'licenses/MPL-2.0.txt',
                'licenses/dependencies.json']
    for relative in required:
        file = stage / relative
        if not file.is_file():
            raise SystemExit(f'Missing {file}; build and fetch dependencies first.')
        if file.suffix in {'.exe', '.dll'} and not is_pe32(file):
            raise SystemExit(f'{file} is not a PE32 i386 executable/DLL.')
    sources = list(source_files())
    if not (ROOT / 'README.md').is_file() or not (ROOT / 'LICENSE').is_file():
        raise SystemExit('README and MIT license are required.')
    # Refresh only generated source/docs copies, preserving runtime/media fixtures.
    source_dir = stage / 'source'
    if source_dir.exists():
        shutil.rmtree(source_dir)
    for file in sources:
        destination = source_dir / file.relative_to(ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, destination)
    for name in ['README.md', 'LICENSE', 'THIRD_PARTY.md']:
        shutil.copy2(ROOT / name, stage / name)
    # The source copy uses repository-relative documentation links. The portable
    # root copy points into source/, where the documents and bridge actually live.
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
            target = readme_path.parent / link.split('#', 1)[0]
            if not target.is_file():
                raise SystemExit(f'Broken packaged README link: {readme_path} -> {link}')
    output.mkdir(parents=True, exist_ok=True)
    files = []
    omitted = []
    for file in sorted(stage.rglob('*')):
        if not file.is_file():
            continue
        rel = file.relative_to(stage)
        if slim and rel.parts[:2] == ('vlc', 'plugins'):
            if len(rel.parts) != 4:
                omitted.append(str(rel)); continue
            family, name = rel.parts[2:]
            allowed = family == 'video_chroma' or name in {
                f'lib{x}_plugin.dll' for x in SLIM_PLUGINS.get(family, [])}
            if not allowed:
                omitted.append(str(rel)); continue
        if slim and rel.parts[:2] in [('vlc', 'lua'), ('vlc', 'hrtfs')]:
            omitted.append(str(rel)); continue
        files.append((file, Path('TubeLiviano') / rel))
    name = f'TubeLiviano_Portable_{VERSION}_x86.zip'
    make_zip(output / name, files)
    vendor_sources = []
    for name_source in ['modern-yt-dlp.tar.gz', 'xp-yt-dlp.tar.gz', 'vlc-3.0.21-source.tar.gz',
                        'quickjs-0.18.0-source.tar.gz', 'yt_dlp_ejs-0.8.0.tar.gz',
                        'certifi-2026.10.23.tar.gz']:
        file = ROOT / '.cache' / name_source
        if not file.is_file():
            raise SystemExit(f'Missing upstream source: {file}. Run fetch-deps.py --sources.')
        vendor_sources.append((file, Path('upstream') / name_source))
    vendor_sources += [(ROOT / 'THIRD_PARTY.md', Path('THIRD_PARTY.md')),
                       (ROOT / 'scripts/fetch-deps.py', Path('recipes/fetch-deps.py')),
                       (stage / 'licenses/dependencies.json', Path('dependencies.json'))]
    source_name = f'TubeLiviano_ThirdParty_Sources_{VERSION}.zip'
    make_zip(output / source_name, vendor_sources)
    report = {'version': VERSION, 'vlc_profile': 'slim' if slim else 'full',
              'portable': {'file': name, 'bytes': (output / name).stat().st_size,
                           'sha256': sha256(output / name)},
              'upstream_sources': {'file': source_name, 'bytes': (output / source_name).stat().st_size,
                                   'sha256': sha256(output / source_name)},
              'files': len(files), 'omitted_vlc_files': omitted}
    (output / 'package-report.json').write_text(json.dumps(report, indent=2) + '\n')
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
