#!/usr/bin/env python3
"""Build-host only: deterministic H.264 baseline/AAC offline test clips."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / "build/portable/media"


def generate(path, height, seconds):
    width = {144: 256, 240: 426, 360: 640}[height]
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", f"testsrc2=size={width}x{height}:rate=24",
        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100",
        "-t", str(seconds), "-c:v", "libx264", "-threads", "2",
        "-profile:v", "baseline", "-level:v", "3.0", "-pix_fmt", "yuv420p",
        "-preset", "fast", "-crf", "26", "-g", "48", "-bf", "0",
        "-c:a", "aac", "-b:a", "48k", "-ac", "1", "-movflags", "+faststart",
        "-metadata", "title=TubeLiviano - imagen y tono 440 Hz", str(path),
    ], check=True)


if __name__ == "__main__":
    DEST.mkdir(parents=True, exist_ok=True)
    for h in (144, 240, 360):
        generate(DEST / f"prueba-{h}.mp4", h, 8)
    long = ROOT / "build/test-media"
    long.mkdir(parents=True, exist_ok=True)
    generate(long / "range-96s.mp4", 240, 96)
