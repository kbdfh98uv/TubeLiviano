#!/usr/bin/env python3
"""Verify the production actor + plugin with an unchanged official engine.

An explicit --surrogate-x64 is mandatory for an x64 engine. The only additional
extractor is a test-only network-free fixture; this does not test YouTube or XP.
The engine SHA-256 must match the cached release's official SHA2-256SUMS.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
# Validation-only asset from the SAME 2026.10.26 release; never packaged.
OFFICIAL_X64_SHA256 = 'f5389d3698ecde4ef06f4e6b1ce0294fe9ac1396228c8990b8d3416d6d177a7d'


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--surrogate-x64', action='store_true')
    parser.add_argument('--cc-prefix', default='i686-w64-mingw32-')
    parser.add_argument('--wine', default='wine')
    parser.add_argument('--wine-prefix', type=Path)
    parser.add_argument('--xvfb')
    parser.add_argument('--report-dir', type=Path, default=ROOT / 'build/reports/worker-windows')
    parser.add_argument('--samples', type=int, default=3)
    args = parser.parse_args()
    if not 1 <= args.samples <= 6:
        parser.error('samples must be 1..6')
    verify = load('worker_verify', 'scripts/verify-offline.py')
    fixtures = load('worker_fixture', 'tests/worker_plugin_test.py')
    expected_machine = 0x8664 if args.surrogate_x64 else 0x14c
    if verify.architecture(args.engine) != expected_machine:
        raise RuntimeError('Engine architecture differs from explicit test mode')
    asset = 'yt-dlp.exe' if args.surrogate_x64 else 'yt-dlp_x86.exe'
    sums = {}
    for line in (ROOT / '.cache/modern-SHA2-256SUMS').read_text().splitlines():
        digest, name = line.split(None, 1)
        sums[name.lstrip('*')] = digest
    digest = hashlib.sha256(args.engine.read_bytes()).hexdigest()
    if sums.get(asset) != digest or (args.surrogate_x64 and digest != OFFICIAL_X64_SHA256):
        raise RuntimeError('Engine differs from official pinned release SHA-256')
    out = args.report_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    summary = {'passed': False, 'scope': 'Unchanged official engine, production C actor/plugin, offline fixture; not YouTube or XP/Atom',
               'surrogate_x64': args.surrogate_x64, 'engine_sha256': digest, 'commands': [], 'samples': []}
    env = dict(os.environ, WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree,mshtml=')
    if args.wine_prefix:
        env['WINEPREFIX'] = str(args.wine_prefix.resolve())
    x = xlog = None

    def run(command, log, timeout=30):
        summary['commands'].append(command)
        with (out / log).open('w') as dest:
            result = subprocess.run(command, env=env, cwd=ROOT, stdout=dest, stderr=dest, timeout=timeout)
        if result.returncode:
            raise RuntimeError('Command failed (%s); inspect %s' % (result.returncode, out / log))

    try:
        stage = Path(tempfile.mkdtemp(prefix='official-worker-', dir=out))
        (stage / 'engines/modern').mkdir(parents=True)
        shutil.copyfile(args.engine, stage / 'engines/modern/yt-dlp.exe')
        plugins = stage / 'plugins/tubeliviano/yt_dlp_plugins/extractor'
        plugins.mkdir(parents=True)
        shutil.copyfile(fixtures.PLUGIN, plugins / 'tubeliviano.py')
        fixture = fixtures.FIXTURE.replace("    def _real_extract(self, url):\n", "    def _real_extract(self, url):\n        with open(os.environ['TL_WORKER_PID_LOG'], 'a') as trace: trace.write(str(os.getpid()) + '\\n')\n")
        (plugins / 'fixture.py').write_text(fixture)
        common = [args.cc_prefix + 'gcc', '-municode', '-std=c99', '-Os', '-DUNICODE', '-D_UNICODE',
                  '-D__USE_MINGW_ANSI_STDIO=0', '-Wall', '-Wextra', '-Werror']
        if not args.surrogate_x64:
            common += ['-march=i686']
        driver = stage / 'driver.exe'
        run(common + ['tests/worker_official_driver.c', 'src/engine_worker.c', 'src/engine.c', 'src/os_detect.c', 'src/engine_process.c',
                       '-static-libgcc', '-Wl,--no-insert-timestamp', '-o', str(driver)], 'compile.txt', 60)
        if verify.architecture(driver) != expected_machine:
            raise RuntimeError('Wrong driver architecture')
        summary['driver_sha256'] = hashlib.sha256(driver.read_bytes()).hexdigest()
        summary['plugin_sha256'] = hashlib.sha256(fixtures.PLUGIN.read_bytes()).hexdigest()
        if args.xvfb:
            x, xlog = verify.start_xvfb(args.xvfb, out, env)
        windows = lambda path: 'Z:' + str(path.resolve()).replace('/', '\\')
        env['TL_WORKER_PROBE_LOG'] = windows(stage / 'probes.txt')
        env['TL_WORKER_PID_LOG'] = windows(stage / 'pids.txt')
        for sample in range(args.samples):
            for file in ('pids.txt', 'probes.txt'):
                (stage / file).unlink(missing_ok=True)
            report = out / ('candidate-%d.json' % sample)
            run([args.wine, str(driver), windows(stage), windows(report)], 'candidate-%d.txt' % sample)
            data = json.loads(report.read_text())
            pids = (stage / 'pids.txt').read_text().splitlines()
            if not data['passed'] or len(pids) != 3 or pids[0] != pids[1] or pids[2] == pids[0]:
                raise RuntimeError('Production actor did not reuse same PID for search/resolve')
            if (stage / 'probes.txt').exists():
                raise RuntimeError('Normal resolve unexpectedly probed media formats')
            data.update(sample=sample, pid=int(pids[0]), fresh_pid=int(pids[2]), same_pid=True)
            summary['samples'].append(data)
        summary['medians'] = {key: statistics.median(row[key] for row in summary['samples'])
                              for key in ('search_ms', 'reused_resolve_ms', 'fresh_resolve_ms', 'cleanup_wait_ms')}
        summary['passed'] = True
    finally:
        if x:
            x.terminate()
            x.wait(timeout=5)
            xlog.close()
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
