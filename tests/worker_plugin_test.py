#!/usr/bin/env python3
"""Offline integration of the production plugin with both verified yt-dlp pins.

Uses the exact cached upstream sources on host Python, not the Windows/XP binary.
The extra test-only extractor intercepts search and serves local fake metadata;
it is copied to a temporary plugin directory and never packaged as runtime code.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import selectors
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / 'plugins/tubeliviano/yt_dlp_plugins/extractor/tubeliviano.py'
SPEC = importlib.util.spec_from_file_location('worker_pins', ROOT / 'scripts/fetch-deps.py')
PINS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PINS)
FIXTURE = r'''
from yt_dlp.extractor.common import InfoExtractor
from yt_dlp.extractor.youtube import YoutubeSearchIE as OriginalSearchIE
import os
class YoutubeSearchIE(OriginalSearchIE):
    def _real_extract(self, url):
        return self.playlist_result([
            {'_type': 'url', 'url': 'https://fixture.invalid/muxed', 'id': 'abcdefghijk',
             'title': 'Caf\u00e9 \U0001f3b5', 'channel': 'Canal de prueba', 'duration': 96.5}],
             playlist_id='fixture-search')
class WorkerFixtureIE(InfoExtractor):
    IE_NAME = 'worker-fixture'
    _VALID_URL = r'https://fixture\.invalid/(?P<id>muxed|split)$'
    def _real_extract(self, url):
        def check_formats(formats, warning=True):
            with open(os.environ['TL_WORKER_PROBE_LOG'], 'a') as output:
                output.write('probe\n')
            yield from formats
        self._downloader._check_formats = check_formats
        formats = []
        for height in (144, 240, 360, 720):
            formats.append({'format_id': 'v%d' % height,
                'url': 'https://cdn.invalid/video-%d.mp4' % height,
                'height': height, 'fps': 24, 'vcodec': 'avc1.42e01e', 'acodec': 'none', 'ext': 'mp4'})
            if self._match_id(url) == 'muxed':
                formats.append({'format_id': 'mux%d' % height,
                    'url': 'https://cdn.invalid/muxed-%d.mp4' % height,
                    'height': height, 'fps': 24, 'vcodec': 'avc1.42e01e', 'acodec': 'mp4a.40.2', 'ext': 'mp4'})
        formats.extend([
            {'format_id': 'audio', 'url': 'https://cdn.invalid/audio.m4a',
             'vcodec': 'none', 'acodec': 'mp4a.40.2', 'ext': 'm4a'},
            {'format_id': 'vp9', 'url': 'https://cdn.invalid/unsupported.webm',
             'height': 1080, 'fps': 60, 'vcodec': 'vp9', 'acodec': 'opus', 'ext': 'webm'}])
        return {'id': 'abcdefghijk', 'title': 'Test video', 'formats': formats}
'''


class Reader:
    def __init__(self, pipe):
        self.pipe = pipe
        self.data = b''

    def read(self, size, timeout=12):
        deadline = time.monotonic() + timeout
        with selectors.DefaultSelector() as selector:
            selector.register(self.pipe, selectors.EVENT_READ)
            while len(self.data) < size:
                if not selector.select(max(0, deadline - time.monotonic())):
                    raise TimeoutError('worker protocol deadline')
                chunk = os.read(self.pipe.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('worker closed before complete reply: %r' % self.data)
                self.data += chunk
        result, self.data = self.data[:size], self.data[size:]
        return result

    def line(self):
        result = b''
        while not result.endswith(b'\n'):
            result += self.read(1)
            if len(result) > 128:
                raise AssertionError('unbounded protocol header')
        return result

    def frame(self):
        prefix, kind, length = self.line().split()
        assert prefix == b'TLW1' and kind in (b'OK', b'ERROR')
        payload = self.read(int(length))
        assert self.read(1) == b'\n' and b'\0' not in payload
        return kind.decode(), payload.decode('utf-8')


def main():
    checks = 0
    reports = []
    for flavor in ('modern', 'xp'):
        archive = ROOT / '.cache' / (flavor + '-yt-dlp.tar.gz')
        if hashlib.sha256(archive.read_bytes()).hexdigest() != PINS.ARTIFACTS[archive.name][1]:
            raise RuntimeError('unverified upstream source: %s' % archive)
        with tempfile.TemporaryDirectory(prefix='tubeliviano-plugin-') as temporary:
            stage = Path(temporary)
            with tarfile.open(archive) as upstream:
                for item in upstream:
                    if item.isfile() and item.name.startswith('yt-dlp/yt_dlp/'):
                        relative = Path(item.name).relative_to('yt-dlp')
                        assert '..' not in relative.parts
                        target = stage / relative
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(upstream.extractfile(item).read())
            plugins = stage / 'plugins/tubeliviano/yt_dlp_plugins/extractor'
            plugins.mkdir(parents=True)
            shutil.copyfile(PLUGIN, plugins / 'tubeliviano.py')
            (plugins / 'fixture.py').write_text(FIXTURE)
            probe_log = stage / 'probes.txt'
            env = dict(os.environ, PYTHONPATH=str(stage), TL_WORKER_PROBE_LOG=str(probe_log))
            command = [sys.executable, '-m', 'yt_dlp', '--ignore-config', '--quiet', '--no-warnings',
                       '--no-progress', '--skip-download', '--no-playlist', '--no-cache-dir',
                       '--no-plugin-dirs', '--plugin-dirs', str(stage / 'plugins'), '--', 'tubeliviano:worker']
            for height, kind, fallback in ((144, 'muxed', False), (240, 'muxed', False),
                                          (360, 'muxed', False), (240, 'split', False), (240, 'muxed', True)):
                started = time.monotonic()
                with (stage / 'stderr.txt').open('wb') as stderr:
                    process = subprocess.Popen(command, cwd=stage, env=env, stdin=subprocess.PIPE,
                                               stdout=subprocess.PIPE, stderr=stderr)
                    try:
                        reader = Reader(process.stdout)
                        assert reader.line() == b'TLW1 READY\n'; checks += 1
                        ready_ms = (time.monotonic() - started) * 1000
                        process.stdin.write(json.dumps({'op': 'search', 'input': 'Caf\u00e9 "\\ \U0001f3b5', 'limit': 6}).encode() + b'\n')
                        process.stdin.flush()
                        reply, payload = reader.frame()
                        results = [json.loads(line) for line in payload.splitlines()]
                        assert reply == 'OK' and len(results) == 1; checks += 1
                        assert results[0]['title'] == 'Caf\u00e9 \U0001f3b5'; checks += 1
                        assert results[0]['duration'] == 96.5; checks += 1
                        before_probe = probe_log.read_text().count('probe') if probe_log.exists() else 0
                        process.stdin.write(json.dumps({'op': 'resolve', 'input': 'https://fixture.invalid/' + kind,
                            'height': height, 'fallback': fallback}).encode() + b'\n')
                        process.stdin.flush()
                        reply, payload = reader.frame()
                        expected = ['https://cdn.invalid/muxed-%d.mp4' % height] if kind == 'muxed' else [
                            'https://cdn.invalid/video-%d.mp4' % height, 'https://cdn.invalid/audio.m4a']
                        assert reply == 'OK' and payload.splitlines() == expected, payload; checks += 1
                        after_probe = probe_log.read_text().count('probe') if probe_log.exists() else 0
                        assert (after_probe > before_probe) if fallback else (after_probe == before_probe); checks += 1
                        # Deliberately keep stdin OPEN: resolve must release the interpreter.
                        assert process.wait(timeout=8) == 0; checks += 1
                        reports.append({'pin': flavor, 'height': height, 'split': kind == 'split',
                                        'fallback': fallback, 'ready_ms': round(ready_ms), 'passed': True})
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait(timeout=5)
                        process.stdin.close()
                        process.stdout.close()
    print(json.dumps({'scope': 'Verified pinned sources on host Python; offline fixture, not Windows/XP or YouTube',
        'checks': checks, 'failed': 0, 'results': reports}, indent=2))
    print('Worker plugin: %d checks; 0 failed' % checks)


if __name__ == '__main__':
    main()
