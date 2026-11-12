#!/usr/bin/env python3
"""Build and exercise local profiles and HTTP Range without any Internet access.

Use --surrogate-x64 explicitly when the host cannot run x86. That mode builds
the same sources with x64 LibVLC and is never reported as validating XP or x86.
Requires the pinned NuGet cache, staged certificates/media, MinGW, Wine and X.
"""
from __future__ import annotations

import argparse
import base64
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, ROOT / file)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def architecture(file):
    data = file.read_bytes()
    if data[:2] != b"MZ" or len(data) < 64:
        raise RuntimeError(f"Not a Windows executable: {file}")
    offset = struct.unpack_from("<I", data, 60)[0]
    if data[offset:offset+4] != b"PE\0\0":
        raise RuntimeError(f"Not a PE executable: {file}")
    return struct.unpack_from("<H", data, offset + 4)[0]


def keep_vlc(relative, plugins):
    if relative.parts[0] in ("include", "lua", "hrtfs") or relative.suffix == ".lib":
        return False
    if relative.parts[0] != "plugins":
        return True
    if len(relative.parts) != 3:
        return False
    _, family, name = relative.parts
    return family == "video_chroma" or name in {
        f"lib{name}_plugin.dll" for name in plugins.get(family, [])}


def prepare_runtime(stage, source_stage, media_source, surrogate, package_module, deps):
    if surrogate:
        archive_path = ROOT / ".cache" / f"videolan.libvlc.windows.{deps.VLC}.nupkg"
        data = archive_path.read_bytes()
        digest = base64.b64encode(hashlib.sha512(data).digest()).decode()
        if digest != deps.VLC_HASH:
            raise RuntimeError("Cached LibVLC NuGet checksum does not match its pin")
        with zipfile.ZipFile(archive_path) as archive:
            prefix = "build/x64/"
            for item in archive.infolist():
                if not item.filename.startswith(prefix) or item.is_dir():
                    continue
                relative = Path(item.filename[len(prefix):])
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("Unsafe NuGet member")
                if keep_vlc(relative, package_module.SLIM_PLUGINS):
                    target = stage / "vlc" / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.read(item))
    else:
        for file in (source_stage / "vlc").rglob("*"):
            if file.is_file() and keep_vlc(file.relative_to(source_stage / "vlc"), package_module.SLIM_PLUGINS):
                target = stage / "vlc" / file.relative_to(source_stage / "vlc")
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
    expected = 0x8664 if surrogate else 0x14c
    for file in (stage / "vlc").rglob("*.dll"):
        if architecture(file) != expected:
            raise RuntimeError(f"Wrong DLL architecture: {file}")
    pem = source_stage / "vlc/certs/cacert.pem"
    manifest = json.loads((source_stage / "licenses/dependencies.json").read_text())
    if hashlib.sha256(pem.read_bytes()).hexdigest() != manifest["ca_bundle"]["sha256"]:
        raise RuntimeError("Staged certificate bundle differs from dependency manifest")
    (stage / "vlc/certs").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(pem, stage / "vlc/certs/cacert.pem")
    shutil.copytree(media_source, stage / "media")


def start_xvfb(executable, report_dir, env):
    for number in range(90, 240):
        endpoint = Path(f"/tmp/.X11-unix/X{number}")
        if endpoint.exists() or Path(f"/tmp/.X{number}-lock").exists():
            continue
        log = (report_dir / "xvfb.log").open("w")
        process = subprocess.Popen([executable, f":{number}", "-screen", "0", "1024x768x24", "-nolisten", "tcp", "-ac"],
                                   stdout=log, stderr=log, env=env)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and process.poll() is None:
            try:
                with socket.socket(socket.AF_UNIX) as probe:
                    probe.connect(str(endpoint))
                env["DISPLAY"] = f":{number}"
                return process, log
            except OSError:
                time.sleep(0.05)
        process.terminate()
        process.wait(timeout=5)
        log.close()
        raise RuntimeError("Xvfb did not become ready; inspect xvfb.log")
    raise RuntimeError("No unused local X display available")


def control_tests(prefix, stage, report_dir, env, summary, run):
    """Compile fixtures away from real media/runtime, then require real checks."""
    controls = stage.parent / (stage.name + "-controls")
    controls.mkdir()
    summary["control_tests"] = {}
    loader = controls / "loader"
    loader.mkdir()
    options = controls / "player-options"
    (options / "vlc").mkdir(parents=True)
    # The loader driver derives its runtime from its own executable directory.
    # Hard links save space while keeping the incompatible core out of app stage.
    shutil.copytree(stage / "vlc", loader / "vlc", copy_function=os.link)
    shutil.copytree(stage / "media", loader / "media", copy_function=os.link)
    common = [prefix + "gcc", "-std=c99", "-D__USE_MINGW_ANSI_STDIO=0", "-Os",
              "-DUNICODE", "-D_UNICODE", "-D_WIN32_WINNT=0x0501", "-DWINVER=0x0501",
              "-Wall", "-Wextra", "-Werror"]
    if summary["architecture"] == "x86":
        common.append("-march=i686")
    libraries = ["-static-libgcc", "-Wl,--no-insert-timestamp", "-luser32", "-lgdi32", "-lshell32"]
    builds = [
        ("engine_fixture", ["tests/engine_fixture.c"], ["-municode"], controls / "engine_fixture.exe"),
        ("engine", ["tests/engine_test.c", "src/engine.c", "src/os_detect.c"], ["-municode", "-DENGINE_TEST_TIMEOUT_MS=5000"], controls / "engine_test.exe"),
        ("os", ["tests/os_detection_test.c", "src/os_detect.c"], [], controls / "os_detection_test.exe"),
        ("gui", ["tests/gui_smoke.c"], ["-municode"], controls / "gui_smoke.exe"),
        ("fake_vlc", ["tests/fake_vlc.c"], ["-shared"], controls / "fake_vlc.dll"),
        ("main", ["tests/main_regression_test.c"], ["-municode"], controls / "main_regression_test.exe"),
        ("decoy", ["tests/vlc_decoy.c"], ["-shared"], loader / "libvlccore.dll"),
        ("loader", ["tests/player_loader_test.c", "src/player.c"], [], loader / "player_loader_test.exe"),
        ("settings", ["tests/settings_test.c", "src/settings.c", "src/ui.c"], ["-ffunction-sections", "-Wl,--gc-sections"], controls / "settings_test.exe"),
        ("options-fixture", ["tests/fake_vlc.c"], ["-shared"], options / "vlc/libvlc.dll"),
        ("player-options", ["tests/player_options_test.c", "src/player.c"], [], options / "player_options_test.exe"),
        ("ui-settings", ["tests/ui_settings_test.c"], ["-municode"], controls / "ui_settings_test.exe"),
        ("batch", ["tests/batch_test.c"], ["-municode"], controls / "batch_test.exe"),
        ("fullscreen", ["tests/fullscreen_test.c"], ["-municode"], controls / "fullscreen_test.exe"),
    ]
    expected = 0x8664 if summary["architecture"] == "x64" else 0x14c
    for name, sources, flags, output in builds:
        run(common + flags + sources + libraries + ["-o", str(output)], f"compile-control-{name}.log")
        if architecture(output) != expected:
            raise RuntimeError(f"Wrong control-test architecture: {name}")
    windows = lambda path: "Z:" + str(path).replace("/", "\\")
    cases = [
        ("os", controls / "os_detection_test.exe", [], r"(\d+) OS routing checks;\s*(\d+) failed"),
        ("engine", controls / "engine_test.exe", [], r"(\d+) checks;\s*(\d+) failed"),
        ("gui", controls / "gui_smoke.exe", [windows(stage / "TubeLiviano.exe")], r"GUI:\s*(\d+) checks,\s*(\d+) failures"),
        ("main", controls / "main_regression_test.exe", [windows(stage / "TubeLiviano.exe"), windows(controls / "fake_vlc.dll")], r"Main regressions:\s*(\d+) checks;\s*(\d+) failures"),
        ("loader", loader / "player_loader_test.exe", [], r"(\d+) player loader checks;\s*(\d+) failed"),
        ("settings", controls / "settings_test.exe", [], r"(\d+) native settings checks;\s*(\d+) failed"),
        ("player-options", options / "player_options_test.exe", [], r"(\d+) player options/controller checks;\s*(\d+) failed"),
        ("ui-settings", controls / "ui_settings_test.exe", [windows(stage / "TubeLiviano.exe")], r"UI/settings:\s*(\d+) checks,\s*(\d+) failures"),
        ("fullscreen", controls / "fullscreen_test.exe", [windows(stage / "TubeLiviano.exe"), windows(controls / "fake_vlc.dll")], r"Fullscreen:\s*(\d+) checks;\s*(\d+) failures"),
        ("fullscreen-media", controls / "fullscreen_test.exe", [windows(stage / "TubeLiviano.exe"), "--real-media", windows(report_dir)], r"Fullscreen real media:\s*(\d+) checks;\s*(\d+) failures"),
        ("batch", controls / "batch_test.exe", [windows(ROOT / "scripts/probar-offline.cmd")], r'"checks"\s*:\s*(\d+)\s*,\s*"failures"\s*:\s*(\d+)'),
    ]
    for name, executable, arguments, pattern in cases:
        command = [env["WINE"], str(executable)] + arguments
        summary["commands"].append(command)
        log = report_dir / f"control-{name}.log"
        result = {"command": command, "log": str(log), "architecture": summary["architecture"],
                  "synthetic_media": name in {"main", "player-options", "batch", "fullscreen"}, "exit_code": None, "checks": 0, "failed": None, "passed": False}
        summary["control_tests"][name] = result
        try:
            with log.open("w") as output:
                process = subprocess.run(command, cwd=ROOT, stdout=output, stderr=output, env=env, timeout=45)
            result["exit_code"] = process.returncode
        except subprocess.TimeoutExpired:
            result["failure"] = "timeout after 45 seconds"
            raise RuntimeError(f"Control test {name} timed out; inspect {log.name}")
        matches = re.findall(pattern, log.read_text(errors="replace"))
        if matches:
            result["checks"], result["failed"] = map(int, matches[-1])
        result["passed"] = result["exit_code"] == 0 and result["checks"] > 0 and result["failed"] == 0
        if not result["passed"]:
            raise RuntimeError(f"Control test {name} failed or executed no checks; inspect {log.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--surrogate-x64", action="store_true", help="explicit validation-only x64 build; never validates delivered x86")
    parser.add_argument("--cc-prefix", help="MinGW tool prefix, e.g. /tools/bin/i686-w64-mingw32-")
    parser.add_argument("--wine", default=os.environ.get("WINE", "wine"))
    parser.add_argument("--wine-prefix", type=Path)
    parser.add_argument("--xvfb", help="optional Xvfb executable; otherwise inherit DISPLAY")
    parser.add_argument("--control-tests", action="store_true", help="also run OS, engine, UI, settings, player-options, main-error and real VLC-loader regressions")
    parser.add_argument("--source-stage", type=Path, default=ROOT / "build/portable")
    parser.add_argument("--report-dir", type=Path, default=ROOT / "build/reports/offline-latest")
    args = parser.parse_args()
    report_dir = args.report_dir.resolve()
    report_dir.mkdir(parents=True, exist_ok=True)
    summary = {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
               "complete": False, "passed": False, "surrogate": args.surrogate_x64,
               "control_tests_requested": args.control_tests,
               "architecture": "x64" if args.surrogate_x64 else "x86", "commands": [],
               "limitations": ["No live YouTube", "No physical XP/Atom execution", "Dummy audio sink; audible sound not checked"]}
    if args.surrogate_x64:
        summary["limitations"].append("Same-source x64 surrogate does not execute or certify the delivered x86 binary")
    summary_path = report_dir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")
    result_names = [f"local-{height}.json" for height in (144, 240, 360)] + ["range-measurement.json"]
    for name in result_names + ["range-96s.json"]:
        (report_dir / name).unlink(missing_ok=True)
    server = server_log = None

    def run(command, log, env=None, timeout=120):
        summary["commands"].append(command)
        with (report_dir / log).open("w") as output:
            result = subprocess.run(command, cwd=ROOT, stdout=output, stderr=output, env=env, timeout=timeout)
        if result.returncode:
            raise RuntimeError(f"Command exited {result.returncode}; inspect {log}")

    try:
        source_stage = args.source_stage.resolve()
        media_source = source_stage / "media"
        missing_profiles = any(not (media_source / f"prueba-{h}.mp4").exists() for h in (144, 240, 360))
        if missing_profiles or not (ROOT / "build/test-media/range-96s.mp4").exists():
            run([sys.executable, "scripts/generate-clips.py"], "generate-clips.log")
            if missing_profiles:
                media_source = ROOT / "build/portable/media"
        stage = Path(tempfile.mkdtemp(prefix="offline-stage-", dir=report_dir))
        summary["stage"] = str(stage)
        package_module = module("tl_package_offline", "scripts/package.py")
        deps = module("tl_deps_offline", "scripts/fetch-deps.py")
        prepare_runtime(stage, source_stage, media_source, args.surrogate_x64, package_module, deps)
        prefix = args.cc_prefix or ("x86_64-w64-mingw32-" if args.surrogate_x64 else "i686-w64-mingw32-")
        resource = stage / "app.o"
        run([prefix + "windres", "src/app.rc", str(resource)], "windres.log")
        command = [prefix + "gcc", "-std=c99", "-D__USE_MINGW_ANSI_STDIO=0", "-Os", "-DUNICODE", "-D_UNICODE",
                   "-D_WIN32_WINNT=0x0501", "-DWINVER=0x0501", "-Wall", "-Wextra", "-Werror", "-ffunction-sections"]
        if not args.surrogate_x64:
            command.append("-march=i686")
        command += ["src/main.c", "src/player.c", "src/engine.c", "src/os_detect.c", "src/settings.c", "src/ui.c", str(resource),
                    "-mwindows", "-static-libgcc", "-s",
                    "-Wl,--gc-sections,--no-insert-timestamp,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1",
                    "-luser32", "-lgdi32", "-lshell32", "-o", str(stage / "TubeLiviano.exe")]
        run(command, "compile.log")
        if architecture(stage / "TubeLiviano.exe") != (0x8664 if args.surrogate_x64 else 0x14c):
            raise RuntimeError("Compiler produced the wrong executable architecture")
        summary["binary_sha256"] = hashlib.sha256((stage / "TubeLiviano.exe").read_bytes()).hexdigest()
        env = os.environ.copy()
        env.update(WINE=args.wine, TL_TEST_STAGE=str(stage), TL_REPORT_DIR=str(report_dir),
                   TL_VALIDATION_ARCH=summary["architecture"], TL_VALIDATION_SURROGATE="1" if args.surrogate_x64 else "0")
        if args.wine_prefix:
            env["WINEPREFIX"] = str(args.wine_prefix.resolve())
        env.setdefault("WINEDEBUG", "-all")
        if args.xvfb:
            server, server_log = start_xvfb(args.xvfb, report_dir, env)
        elif not env.get("DISPLAY"):
            raise RuntimeError("No DISPLAY; pass --xvfb or provide a working X server")
        run([sys.executable, "tests/playback.py"], "playback.log", env, timeout=360)
        summary["results"] = {name: json.loads((report_dir / name).read_text()) for name in result_names}
        if not all(item.get("complete") and item.get("passed") for item in summary["results"].values()):
            raise RuntimeError("A playback report did not demonstrate completion and success")
        if any(item.get("binary_sha256") != summary["binary_sha256"] or
               item.get("validation_architecture") != summary["architecture"]
               for item in summary["results"].values()):
            raise RuntimeError("A report belongs to a different executable or architecture")
        if args.control_tests:
            control_tests(prefix, stage, report_dir, env, summary, run)
        summary["passed"] = True
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        summary["failure"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        if server:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
        if server_log:
            server_log.close()
        summary["complete"] = True
        summary["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        summary_path.write_text(json.dumps(summary, indent=2) + "\n")
        print(f"Offline verification {'PASS' if summary['passed'] else 'FAIL'}: {summary_path}")


if __name__ == "__main__":
    raise SystemExit(main())
