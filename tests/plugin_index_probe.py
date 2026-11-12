#!/usr/bin/env python3
"""Probe real VLC's index in moved and unwritable folders after startup benchmark.

Pass the run-* directory from scripts/benchmark-startup.py. This checks the
compiled player used by that run, not physical XP/Atom or audible sound. Linux
folder permissions must actually deny writes; running as root cannot prove that.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark-dir', required=True, type=Path)
    parser.add_argument('--report-dir', required=True, type=Path)
    parser.add_argument('--wine', default=os.environ.get('WINE', 'wine'))
    parser.add_argument('--wine-prefix', type=Path)
    parser.add_argument('--xvfb', required=True, type=Path)
    args = parser.parse_args()
    args.report_dir.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='index-', dir=args.report_dir.resolve()))
    source = args.benchmark_dir.resolve()
    spec = importlib.util.spec_from_file_location('index_verify', ROOT / 'scripts/verify-offline.py')
    verify = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verify)
    env = os.environ.copy()
    env.setdefault('WINEDEBUG', '-all')
    env.setdefault('WINEDLLOVERRIDES', 'mscoree,mshtml=')
    if args.wine_prefix:
        env['WINEPREFIX'] = str(args.wine_prefix.resolve())
    server, log = verify.start_xvfb(args.xvfb, output, env)
    summary = {'scope': 'Real VLC/player from supplied benchmark, generated local media, dummy audio; Unix folder permissions, not XP ACL/physical USB',
               'passed': False, 'results': [], 'benchmark_dir': str(source)}
    windows = lambda path: 'Z:' + str(path).replace('/', '\\')
    try:
        for case, indexed, readonly in [('readonly-existing', True, True), ('readonly-missing', False, True), ('reubicado café', True, False)]:
            stage = output / case
            shutil.copytree(source / 'candidate-stage', stage)
            folder, index = stage / 'vlc/plugins', stage / 'vlc/plugins/plugins.dat'
            if not indexed:
                index.unlink(missing_ok=True)
            denied = False
            try:
                if readonly:
                    folder.chmod(0o555)
                    try:
                        with (folder / 'write-probe').open('wb') as stream:
                            stream.write(b'probe')
                    except PermissionError:
                        denied = True
                    if not denied:
                        raise RuntimeError('Host did not enforce write protection; no readonly proof')
                report = output / (case + '.json')
                command = [args.wine, str(source / 'candidate.exe'), '--base', windows(stage), '--url', windows(stage / 'media/prueba-240.mp4'),
                           '--report', windows(report), '--async-prepare', '1']
                with (output / (case + '.txt')).open('w') as stream:
                    result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=stream, timeout=35)
                row = json.loads(report.read_text())
                row.update(case=case, index_before=indexed, index_after=index.exists(), folder_write_denied=denied,
                           command=command, benchmark_sha256=hashlib.sha256((source / 'candidate.exe').read_bytes()).hexdigest())
                summary['results'].append(row)
                if result.returncode or not row.get('passed'):
                    raise RuntimeError('Playback failed: ' + case)
            finally:
                folder.chmod(0o755)
        summary['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        summary['failure'] = str(error)
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)
        log.close()
        (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(('PASS' if summary['passed'] else 'FAIL') + ': ' + str(output / 'summary.json'))
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
