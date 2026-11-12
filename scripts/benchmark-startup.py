#!/usr/bin/env python3
"""Measure visible startup with real VLC, generated media and a simulated resolver.

Requires an already validated runtime with the reduced 69-plugin profile. The
runtime is copied privately; no index or file in the supplied stage is changed.
Use --surrogate-x64 explicitly for x64/Wine; that does not validate XP/x86.
An optional baseline directory supplies player.c/player.h from the old version.
"""
from __future__ import annotations

import argparse
import hashlib
import http.server
import importlib.util
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--baseline-player-dir", type=Path)
    parser.add_argument("--cc-prefix")
    parser.add_argument("--wine", default=os.environ.get("WINE", "wine"))
    parser.add_argument("--wine-prefix", type=Path)
    parser.add_argument("--xvfb", type=Path, help="otherwise inherit DISPLAY")
    parser.add_argument("--surrogate-x64", action="store_true")
    parser.add_argument("--report-dir", type=Path, default=ROOT / "build/reports/startup")
    parser.add_argument("--samples", type=int, default=4)
    parser.add_argument("--resolve-ms", type=int, default=900, help="explicit synthetic extraction delay")
    parser.add_argument("--cache-ms", type=int, default=450)
    parser.add_argument("--prefetch-kib", type=int, choices=(64, 128, 256), default=128)
    args = parser.parse_args()
    if not 1 <= args.samples <= 20 or not 0 <= args.resolve_ms <= 5000 or not 250 <= args.cache_ms <= 3000:
        parser.error("samples must be 1..20, resolve-ms 0..5000, cache-ms 250..3000")
    runtime, output = args.runtime.resolve(), args.report_dir.resolve()
    if output == runtime or runtime in output.parents:
        parser.error("report-dir must be outside the supplied runtime")
    output.mkdir(parents=True, exist_ok=True)
    run_dir = Path(tempfile.mkdtemp(prefix="run-", dir=output))
    summary = {"complete": False, "passed": False, "surrogate": args.surrogate_x64,
               "architecture": "x64" if args.surrogate_x64 else "x86", "run_dir": str(run_dir),
               "scope": "Real VLC; generated SMPTE video; dummy audio; HTTP Range at 64 KiB/s; "
                        "simulated resolver; fresh processes with OS file cache warm. No YouTube or physical XP/Atom.",
               "synthetic_resolver_ms": args.resolve_ms, "commands": [], "reports": [], "groups": {}}
    env = os.environ.copy()
    env.setdefault("WINEDEBUG", "-all")
    env.setdefault("WINEDLLOVERRIDES", "mscoree,mshtml=")
    if args.wine_prefix:
        env["WINEPREFIX"] = str(args.wine_prefix.resolve())
    xserver = xlog = None

    def run(command, log):
        summary["commands"].append(command)
        with (run_dir / log).open("w") as stream:
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=stream, timeout=45)
        if result.returncode:
            raise RuntimeError(f"Exit {result.returncode}; inspect {run_dir / log}")

    try:
        verify = module("tl_startup_verify", "scripts/verify-offline.py")
        playback = module("tl_startup_playback", "tests/playback.py")
        expected = 0x8664 if args.surrogate_x64 else 0x14c
        plugins = sorted((runtime / "vlc/plugins").glob("*/*.dll"))
        if len(plugins) != 69:
            raise RuntimeError(f"Expected reduced 69-plugin runtime; found {len(plugins)}")
        dlls = sorted((runtime / "vlc").rglob("*.dll"))
        for file in dlls:
            if verify.architecture(file) != expected:
                raise RuntimeError(f"Architecture differs from declared {summary['architecture']}: {file}")
        summary["runtime_sha256"] = {str(file.relative_to(runtime)): digest(file) for file in dlls}
        media = ROOT / "build/test-media/range-96s.mp4"
        clip = runtime / "media/prueba-240.mp4"
        if not media.is_file() or not clip.is_file():
            raise RuntimeError("Missing generated clips; run scripts/generate-clips.py first")
        data = media.read_bytes()
        summary["media_sha256"] = {"range-96s.mp4": digest(media), "prueba-240.mp4": digest(clip)}
        source = run_dir / "source"
        (source / "tests").mkdir(parents=True)
        shutil.copyfile(ROOT / "tests/startup_bench.c", source / "tests/startup_bench.c")
        cases = [("candidate", ROOT / "src", 1)]
        if args.baseline_player_dir:
            cases.insert(0, ("baseline", args.baseline_player_dir.resolve(), 0))
        prefix = args.cc_prefix or ("x86_64-w64-mingw32-" if args.surrogate_x64 else "i686-w64-mingw32-")
        stages = {}
        for name, player_dir, async_mode in cases:
            target = source / ("src" if name == "candidate" else name)
            target.mkdir(exist_ok=True)
            for filename in ("player.c", "player.h"):
                shutil.copyfile(player_dir / filename, target / filename)
            stage = run_dir / (name + "-stage")
            shutil.copytree(runtime / "vlc", stage / "vlc", ignore=shutil.ignore_patterns("plugins.dat"))
            (stage / "media").mkdir()
            shutil.copyfile(clip, stage / "media/prueba-240.mp4")
            stages[name] = stage
            command = [prefix + "gcc", "-municode", "-std=c99", "-Os", "-DUNICODE", "-D_UNICODE",
                       "-D__USE_MINGW_ANSI_STDIO=0", "-Wall", "-Wextra", "-Werror"]
            if name == "baseline":
                command.append("-DBENCH_SYNC_BASELINE")
            if not args.surrogate_x64:
                command.append("-march=i686")
            command += [str(source / "tests/startup_bench.c"), str(target / "player.c"), "-static-libgcc",
                        "-luser32", "-lgdi32", "-Wl,--no-insert-timestamp", "-o", str(run_dir / (name + ".exe"))]
            # The benchmark includes ../src/player.h, including for the old implementation.
            if name == "baseline" and not (source / "src").exists():
                (source / "src").mkdir()
                shutil.copyfile(ROOT / "src/player.h", source / "src/player.h")
            run(command, "compile-" + name + ".log")
            if verify.architecture(run_dir / (name + ".exe")) != expected:
                raise RuntimeError("Compiler produced the wrong benchmark architecture")
        summary["source_sha256"] = {str(file.relative_to(source)): digest(file) for file in sorted(source.rglob("*")) if file.is_file()}
        summary["benchmark_sha256"] = {name: digest(run_dir / (name + ".exe")) for name, _, _ in cases}
        if args.xvfb:
            xserver, xlog = verify.start_xvfb(args.xvfb, run_dir, env)
        elif not env.get("DISPLAY"):
            raise RuntimeError("No DISPLAY; pass --xvfb or provide an X server")
        windows = lambda path: "Z:" + str(path).replace("/", "\\")

        def measure(name, sample, async_mode, prime=False):
            stage = stages[name]
            index = stage / "vlc/plugins/plugins.dat"
            if name == "baseline":
                index.unlink(missing_ok=True)
            before = index.exists()
            server = None
            label = "candidate-index-prime" if prime else f"{name}-{sample}"
            report = run_dir / (label + ".json")
            if prime:
                url = windows(stage / "media/prueba-240.mp4")
            else:
                origin = type("Origin", (playback.Origin,), {"data": data, "sent": 0, "ranges": [], "first_byte_at": None})
                server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), origin)
                threading.Thread(target=server.serve_forever, daemon=True).start()
                url = f"http://127.0.0.1:{server.server_port}/clip.mp4"
            command = [args.wine, str(run_dir / (name + ".exe")), "--base", windows(stage), "--url", url,
                       "--report", windows(report), "--resolve-ms", "0" if prime else str(args.resolve_ms),
                       "--async-prepare", str(async_mode), "--cache-ms", str(args.cache_ms), "--prefetch-kib", str(args.prefetch_kib)]
            try:
                run(command, label + ".log")
            finally:
                if server:
                    server.shutdown()
                    server.server_close()
            row = json.loads(report.read_text())
            row.update(group=name, sample=sample, index_before=before, index_after=index.exists(), index_priming=prime)
            if server:
                row.update(origin_sent_bytes=origin.sent, range_requests=origin.ranges, file_bytes=len(data))
            report.write_text(json.dumps(row, indent=2) + "\n")
            summary["reports"].append(row)
            if not row.get("complete") or not row.get("passed") or row.get("first_visible_ms", -1) < 0:
                raise RuntimeError(f"No visible-video/audio proof: {report}")
            if prime and not index.is_file():
                raise RuntimeError("Candidate did not generate its native plugin index")
            print(f"{label}: first visible {row['first_visible_ms']} ms", flush=True)

        measure("candidate", -1, 1, prime=True)
        for sample in range(args.samples):
            for name, _, async_mode in cases:
                measure(name, sample, async_mode)
        for name, _, _ in cases:
            rows = [row for row in summary["reports"] if row["group"] == name and not row["index_priming"]]
            summary["groups"][name] = {"samples": len(rows), "first_visible_ms_median": statistics.median(row["first_visible_ms"] for row in rows)}
        summary["passed"] = True
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        summary["failure"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if xserver:
            xserver.terminate()
            try:
                xserver.wait(timeout=5)
            except subprocess.TimeoutExpired:
                xserver.kill()
                xserver.wait(timeout=5)
        if xlog:
            xlog.close()
        summary["complete"] = True
        (run_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        print(f"Benchmark {'PASS' if summary['passed'] else 'FAIL'}: {run_dir / 'summary.json'}")


if __name__ == "__main__":
    raise SystemExit(main())
