#!/usr/bin/env python3
"""Fetch pinned, verified native Windows x86 dependencies on the build host.

No Python runtime is installed in, or required by, the TubeLiviano frontend.
The bundled upstream yt-dlp executables have their own embedded interpreters.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache'
MODERN = '2026.10.26'
XP = '2026.10.29.091737'
VLC = '3.0.21'
QJS = '0.18.0'
CERTIFI = '2026.10.23'
NUGET = ('https://api.nuget.org/v3-flatcontainer/videolan.libvlc.windows/'
         f'{VLC}/videolan.libvlc.windows.{VLC}.nupkg')
CATALOG = ('https://api.nuget.org/v3/catalog0/data/2024.09.16.10.28.22/'
           'videolan.libvlc.windows.3.0.21.json')
VLC_HASH = ('R9J1L6RkH55i1A9uj/55od/DJYhZBoF1iEX+TbT4OUghKFQsd1BTaiBxJ4OeU570'
            'mNWZVUqNyz0hfGioe7QY/A==')

# URL, SHA-256. Upstream release sum lists are additionally checked below.
ARTIFACTS = {
    'certifi-2026.10.23.tar.gz': (
        'https://files.pythonhosted.org/packages/fd/11/b524cbd9bbdaca58608e4cf4053fd463417549e1959fa8d3aa18319f9555/certifi-2026.10.23.tar.gz',
        'a9895e6e2758b371e0545e321a3a8fd1fcfd9829ada8824a936dfb16aaaf36cd'),
    'MPL-2.0.txt': (
        'https://raw.githubusercontent.com/spdx/license-list-data/v3.27.0/text/MPL-2.0.txt',
        '66a3107d5ad6a058aab753eaac2047ccb2ed0e39465dd0fe5844da3e300d5172'),
    'yt_dlp_ejs-0.8.0.tar.gz': (
        'https://files.pythonhosted.org/packages/d3/e6/cceb9530e8f4e5940f6f7822d90e9d94f1b85343329a16baaf47bbbb3de1/yt_dlp_ejs-0.8.0.tar.gz',
        'd5fa1639f63b5c4af8d932495f60689d5370f1a095782c944f7f62a303eb104e'),
    'quickjs-0.18.0-source.tar.gz': (
        f'https://codeload.github.com/quickjs-ng/quickjs/tar.gz/refs/tags/v{QJS}',
        '28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd'),
    'quickjs-LICENSE': (
        f'https://raw.githubusercontent.com/quickjs-ng/quickjs/v{QJS}/LICENSE',
        '96f73f9d2a16c21a36b418f06073be26e7d6d5e7c1bc99756b21a4f2c74ef171'),
    'modern-yt-dlp_x86.exe': (
        f'https://github.com/yt-dlp/yt-dlp/releases/download/{MODERN}/yt-dlp_x86.exe',
        'c0407c66361f50a99e10949ae882ea2ad7de9086b75d226d8fd88da4f74828fa'),
    'xp-yt-dlp_x86_winXP.exe': (
        f'https://github.com/nicolaasjan/yt-dlp/releases/download/{XP}/yt-dlp_x86_winXP.exe',
        'b039efe2d457971750a0580a63097e59922a337c2945f46a7de0c5ac87c75c12'),
    'modern-yt-dlp.tar.gz': (
        f'https://github.com/yt-dlp/yt-dlp/releases/download/{MODERN}/yt-dlp.tar.gz',
        '21f942d258d25cc4e60b4982a25c423460f0bebb835f38e6dfd1fcb140087f95'),
    'xp-yt-dlp.tar.gz': (
        f'https://github.com/nicolaasjan/yt-dlp/releases/download/{XP}/yt-dlp.tar.gz',
        '78ad302bfc49c3a589a1ad5b2fea0667a46567287c117fa74fa1eaa60958bd35'),
    'vlc-3.0.21-source.tar.gz': (
        'https://codeload.github.com/videolan/vlc/tar.gz/refs/tags/3.0.21',
        '850e20e8f6e217f1b9fe740a33c4b128897726bb9b30120ee2d012db583fdb7a'),
    'vlc-COPYING': (
        'https://raw.githubusercontent.com/videolan/vlc/3.0.21/COPYING',
        '8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643'),
    'vlc-COPYING.LIB': (
        'https://raw.githubusercontent.com/videolan/vlc/3.0.21/COPYING.LIB',
        'dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551'),
    'yt-dlp-LICENSE': (
        f'https://raw.githubusercontent.com/yt-dlp/yt-dlp/{MODERN}/LICENSE',
        '7e12e5df4bae12cb21581ba157ced20e1986a0508dd10d0e8a4ab9a4cf94e85c'),
    'yt-dlp-THIRD_PARTY_LICENSES.txt': (
        f'https://raw.githubusercontent.com/yt-dlp/yt-dlp/{MODERN}/THIRD_PARTY_LICENSES.txt',
        '472aefe951c7db35e1657c1d13fd337140511ed6f2b329205105ad441c5a02b7'),
}


def download(url: str) -> bytes:
    # urllib uses the inherited HTTPS proxy and CA trust; verification stays on.
    print('GET', url, flush=True)
    with urllib.request.urlopen(url, timeout=180) as response:
        return response.read()


def cached(name: str, url: str, digest: str, algorithm='sha256', encoded=False) -> Path:
    path = CACHE / name
    def matches(data):
        hashed = hashlib.new(algorithm, data)
        actual = base64.b64encode(hashed.digest()).decode() if encoded else hashed.hexdigest()
        return actual == digest
    if path.exists():
        if not matches(path.read_bytes()):
            raise SystemExit(f'Checksum mismatch in cached {path}; remove it and retry.')
        return path
    data = download(url)
    if not matches(data):
        raise SystemExit(f'Checksum mismatch downloading {name}; refusing artifact.')
    tmp = path.with_suffix(path.suffix + '.part')
    tmp.write_bytes(data)
    tmp.replace(path)
    return path


def build_quickjs(stage: Path):
    """Build one single-threaded QuickJS x86 runtime for both Windows families.

    The official QJS-ng x86 binary imports Vista's native condition-variable
    APIs and cannot be loaded on XP. Disable os.Worker/Atomics; EJS's ordinary
    single-threaded JavaScript remains enabled. Keep this source modification
    explicit and provide the pristine source and recipe to downstream users.
    """
    name = f'quickjs-{QJS}-source.tar.gz'
    url, digest = ARTIFACTS[name]
    source_archive = cached(name, url, digest)
    source = ROOT / 'build' / 'vendor' / 'quickjs-xp'
    source.mkdir(parents=True, exist_ok=True)
    with tarfile.open(source_archive) as archive:
        archive.extractall(source, filter='data')
    source = source / f'quickjs-{QJS}'
    header = source / 'cutils.h'
    text = header.read_text()
    original = ('#if defined(EMSCRIPTEN) || defined(__wasi__) || defined(__DJGPP)'
                '\n\n#define JS_HAVE_THREADS 0')
    replacement = ('#if defined(EMSCRIPTEN) || defined(__wasi__) || defined(__DJGPP)'
                   ' || defined(TUBELIVIANO_QJS_XP)\n\n#define JS_HAVE_THREADS 0')
    if text.count(original) != 1:
        raise SystemExit('QuickJS source changed; XP thread-disable patch does not apply.')
    header.write_text(text.replace(original, replacement))
    # Upstream's Windows poll branch assumes Workers are enabled; guard its two
    # Worker-only blocks, matching the existing guards in the POSIX branch.
    libc = source / 'quickjs-libc.c'
    text = libc.read_text()
    end = text.index('#else // !defined(_WIN32)', text.index('static int js_os_poll_internal'))
    windows, rest = text[:end], text[end:]
    marker = 'if (flags & JS_OS_POLL_WORKERS) {'
    starts, cursor = [], 0
    while (at := windows.find(marker, cursor)) >= 0:
        starts.append(at)
        cursor = at + len(marker)
    if len(starts) != 2:
        raise SystemExit('QuickJS Windows worker guards changed; review the XP patch.')
    for at in reversed(starts):
        brace, depth, cursor = windows.index('{', at), 1, windows.index('{', at) + 1
        while depth:
            depth += (windows[cursor] == '{') - (windows[cursor] == '}')
            cursor += 1
        start = windows.rfind('\n', 0, at) + 1
        windows = windows[:start] + '#ifdef USE_WORKER\n' + windows[start:cursor] + '\n#endif' + windows[cursor:]
    libc.write_text(windows + rest)
    compiler = os.environ.get('CC', 'i686-w64-mingw32-gcc')
    if not shutil.which(compiler):
        fallback = Path('/workspace/toolchain/root/usr/bin') / compiler
        if fallback.is_file():
            compiler = str(fallback)
        else:
            raise SystemExit('Install i686-w64-mingw32-gcc to build XP QuickJS.')
    output = stage / 'runtimes' / 'qjs.exe'
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = output.with_suffix('.tmp.exe')
    cmd = [compiler, '-std=gnu11', '-Os', '-fwrapv', '-DNDEBUG', '-D_GNU_SOURCE',
           '-DWIN32_LEAN_AND_MEAN', '-D_WIN32_WINNT=0x0501', '-D__STDC_NO_ATOMICS__=1',
           '-DTUBELIVIANO_QJS_XP', '-DQUICKJS_NG_BUILD', '-static', '-static-libgcc', '-s',
           '-Wl,--major-subsystem-version,5,--minor-subsystem-version,1', '-o', str(temporary_output),
           'qjs.c', 'gen/repl.c', 'gen/standalone.c', 'quickjs.c', 'dtoa.c', 'libregexp.c',
           'libunicode.c', 'quickjs-libc.c', '-lm']
    print('Building single-threaded XP QuickJS x86', flush=True)
    subprocess.run(cmd, cwd=source, check=True)
    objdump = shutil.which('i686-w64-mingw32-objdump') or shutil.which('objdump')
    if not objdump:
        raise SystemExit('objdump is required to audit QuickJS XP imports.')
    imports = subprocess.check_output([objdump, '-p', str(temporary_output)], text=True)
    for forbidden in ['InitOnceExecuteOnce', 'ConditionVariable', 'GetTickCount64',
                      'GetSystemTimePreciseAsFileTime', 'GetFinalPathNameByHandle']:
        if forbidden in imports:
            raise SystemExit(f'QuickJS XP audit failed: imports {forbidden}')
    temporary_output.replace(output)
    return hashlib.sha256(output.read_bytes()).hexdigest()


def stage_ca_bundle(stage: Path):
    """Ship Mozilla roots independently of XP's stale system certificate store."""
    name = f'certifi-{CERTIFI}.tar.gz'
    url, digest = ARTIFACTS[name]
    source_archive = cached(name, url, digest)
    with tarfile.open(source_archive) as archive:
        pem = archive.extractfile(f'certifi-{CERTIFI}/certifi/cacert.pem').read()
        notice = archive.extractfile(f'certifi-{CERTIFI}/LICENSE').read()
    certificate_count = pem.count(b'-----BEGIN CERTIFICATE-----')
    if certificate_count < 100:
        raise SystemExit('Unexpectedly small Mozilla CA bundle; refusing it.')
    certificate = stage / 'vlc' / 'certs' / 'cacert.pem'
    certificate.parent.mkdir(parents=True, exist_ok=True)
    certificate.write_bytes(pem)
    # Parse the actual bundled certificates, retaining all TLS checks. This does
    # not claim any remote HTTPS operation works in an unavailable XP machine.
    trust = ssl.create_default_context(cafile=str(certificate))
    # Managed hosts can add their own proxy root to SSLContext. Require at least
    # every bundle certificate; do not mistake that extra trusted root for loss.
    parsed_count = trust.cert_store_stats()['x509']
    if parsed_count < certificate_count:
        raise SystemExit('Mozilla CA bundle did not parse completely.')
    licenses = stage / 'licenses'
    licenses.mkdir(parents=True, exist_ok=True)
    (licenses / 'certifi-LICENSE').write_bytes(notice)
    mpl_url, mpl_hash = ARTIFACTS['MPL-2.0.txt']
    shutil.copy2(cached('MPL-2.0.txt', mpl_url, mpl_hash), licenses / 'MPL-2.0.txt')
    return {'certifi': CERTIFI, 'certificates': certificate_count,
            'sha256': hashlib.sha256(pem).hexdigest(),
            'path': 'vlc/certs/cacert.pem', 'source': name,
            'purpose': 'Additional portable Mozilla trust roots; TLS verification stays enabled'}


def fetch(stage: Path, sources: bool):
    CACHE.mkdir(exist_ok=True)
    # Check official package catalog hash, even when the package is cached.
    catalog = json.loads(download(CATALOG))
    if catalog['packageHashAlgorithm'] != 'SHA512' or catalog['packageHash'] != VLC_HASH:
        raise SystemExit('NuGet catalog hash differs from pinned LibVLC package hash.')
    package = cached(f'videolan.libvlc.windows.{VLC}.nupkg', NUGET,
                     VLC_HASH, 'sha512', True)
    qjs_hash = build_quickjs(stage)
    for flavor, owner, version, asset in [
        ('modern', 'yt-dlp/yt-dlp', MODERN, 'yt-dlp_x86.exe'),
        ('xp', 'nicolaasjan/yt-dlp', XP, 'yt-dlp_x86_winXP.exe'),
    ]:
        sums_url = f'https://github.com/{owner}/releases/download/{version}/SHA2-256SUMS'
        sums = download(sums_url)
        (CACHE / (flavor + '-SHA2-256SUMS')).write_bytes(sums)
        published = {line.split(None, 1)[1].lstrip('*'): line.split(None, 1)[0]
                     for line in sums.decode().splitlines() if line.strip()}
        name = flavor + '-' + asset
        url, digest = ARTIFACTS[name]
        if published.get(asset) != digest:
            raise SystemExit(f'Published checksum for {asset} differs from pin.')
        src = cached(name, url, digest)
        target = stage / 'engines' / flavor / 'yt-dlp.exe'
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
    # Preserve the complete native runtime until tests justify a smaller profile.
    with zipfile.ZipFile(package) as archive:
        for item in archive.infolist():
            if not item.filename.startswith('build/x86/') or item.is_dir():
                continue
            rel = Path(item.filename[len('build/x86/'):])
            if rel.is_absolute() or '..' in rel.parts:
                raise SystemExit('Unsafe archive member')
            if rel.parts[0] == 'include' or rel.suffix == '.lib':
                continue
            target = stage / 'vlc' / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(item))
    licenses = stage / 'licenses'
    licenses.mkdir(parents=True, exist_ok=True)
    ca_bundle = stage_ca_bundle(stage)
    for name in ['vlc-COPYING', 'vlc-COPYING.LIB', 'yt-dlp-LICENSE', 'quickjs-LICENSE',
                 'yt-dlp-THIRD_PARTY_LICENSES.txt']:
        url, digest = ARTIFACTS[name]
        shutil.copy2(cached(name, url, digest), licenses / name)
    manifest = {'vlc': VLC, 'modern_yt_dlp': MODERN, 'xp_yt_dlp_fork': XP, 'quickjs_ng': QJS,
                'ca_bundle': ca_bundle,
                'quickjs_build': {'target': 'i686 Windows XP 5.01, single-threaded',
                                  'disabled': ['os.Worker', 'Atomics'], 'sha256': qjs_hash,
                                  'recipe': 'source/scripts/fetch-deps.py:build_quickjs'},
                'artifacts': {n: {'url': u, 'sha256': h} for n, (u, h) in ARTIFACTS.items()},
                'vlc_nuget': {'url': NUGET, 'sha512_base64': VLC_HASH}}
    (licenses / 'dependencies.json').write_text(json.dumps(manifest, indent=2) + '\n')
    if sources:
        for name in ['modern-yt-dlp.tar.gz', 'xp-yt-dlp.tar.gz', 'vlc-3.0.21-source.tar.gz',
                     'quickjs-0.18.0-source.tar.gz', 'yt_dlp_ejs-0.8.0.tar.gz']:
            url, digest = ARTIFACTS[name]
            cached(name, url, digest)
    print('Native x86 runtime staged at', stage)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, default=ROOT / 'build' / 'portable')
    parser.add_argument('--sources', action='store_true', help='also fetch upstream source archives')
    args = parser.parse_args()
    fetch(args.stage.resolve(), args.sources)
