#!/usr/bin/env python3
"""TubeLiviano 0.5 bridge, for a separate, reasonably powerful computer.

Python and FFmpeg are never required on the netbook. No third-party Python
packages: yt-dlp and FFmpeg are normal executable dependencies on this host.
"""
from __future__ import annotations

import argparse
import hmac
import json
import os
from pathlib import Path
import queue
import select
import signal
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

HEIGHTS = (144, 240, 360)
YOUTUBE_HOSTS = {"youtube.com", "www.youtube.com", "m.youtube.com",
                 "music.youtube.com", "youtu.be", "www.youtu.be"}


class BridgeError(Exception):
    pass


def youtube_url(value: str) -> bool:
    """Reject arbitrary Internet/LAN URLs: this endpoint is not an open proxy."""
    try:
        parsed = urlsplit(value)
        return (len(value) <= 2048 and parsed.scheme in ("http", "https")
                and parsed.hostname in YOUTUBE_HOSTS
                and parsed.username is None and parsed.password is None
                and parsed.port in (None, 80, 443)
                and not any(ord(c) < 32 for c in value))
    except ValueError:
        return False


def launch(args: list[str]) -> subprocess.Popen:
    options = {"stdin": subprocess.DEVNULL, "stdout": subprocess.PIPE,
               "stderr": subprocess.PIPE, "bufsize": 0}
    if os.name == "nt":
        options["creationflags"] = subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        options["start_new_session"] = True
    return subprocess.Popen(args, **options)


def stop_tree(process: subprocess.Popen | None) -> None:
    if process is None:
        return
    # Also terminate helper children (for example a yt-dlp JavaScript runtime).
    if os.name == "nt":
        if process.poll() is None:
            try:
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
            except (OSError, subprocess.TimeoutExpired):
                # FFmpeg has no children. Preserve direct-child cleanup even if
                # an administrator removed taskkill from the powerful host.
                process.kill()
    else:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            process.kill()
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.wait(timeout=5)
    for pipe in (process.stdout, process.stderr):
        if pipe:
            pipe.close()


def pipe_reader(pipe, output: queue.Queue, stopped: threading.Event) -> None:
    """Small bounded queue avoids storing a video, even if VLC stops reading."""
    try:
        while not stopped.is_set():
            block = os.read(pipe.fileno(), 16384)
            if not block:
                break
            while not stopped.is_set():
                try:
                    output.put(block, timeout=0.2)
                    break
                except queue.Full:
                    pass
    except (OSError, ValueError):
        pass
    finally:
        while not stopped.is_set():
            try:
                output.put(None, timeout=0.2)
                break
            except queue.Full:
                pass


def drain_stderr(pipe, tail: bytearray) -> None:
    try:
        while True:
            chunk = os.read(pipe.fileno(), 4096)
            if not chunk:
                return
            tail.extend(chunk)
            if len(tail) > 8192:
                del tail[:-8192]
    except (OSError, ValueError):
        pass


def ffmpeg_command(executable: str, sources: list[str], height: int,
                   realtime: bool = True) -> list[str]:
    if height not in HEIGHTS or not 1 <= len(sources) <= 2:
        raise ValueError("Perfil o cantidad de entradas inválidos")
    command = [executable, "-hide_banner", "-loglevel", "error", "-nostdin"]
    for source in sources:
        if realtime:
            command += ["-re"]
        if source.startswith(("http://", "https://")):
            command += ["-rw_timeout", "15000000"]
        command += ["-i", source]
    command += ["-map", "0:v:0", "-map", "1:a:0?" if len(sources) == 2 else "0:a:0?",
                "-vf", f"scale=-2:trunc(min({height}\\,ih)/2)*2,fps=24",
                "-c:v", "libx264", "-profile:v", "baseline", "-level:v", "3.0",
                "-pix_fmt", "yuv420p", "-preset", "veryfast", "-tune", "zerolatency",
                "-threads", "2", "-bf", "0", "-g", "48", "-keyint_min", "24",
                "-sc_threshold", "0", "-b:v", {144: "150k", 240: "280k", 360: "500k"}[height],
                "-maxrate", {144: "200k", 240: "400k", 360: "700k"}[height],
                "-bufsize", "700k", "-c:a", "aac", "-b:a", "64k", "-ac", "2",
                "-ar", "44100", "-f", "mpegts", "-muxdelay", "0", "-muxpreload", "0",
                "-flush_packets", "1", "pipe:1"]
    return command


def resolver_command(options, url: str) -> list[str]:
    selector = ("bestvideo[height<=720]+bestaudio/"
                "best[height<=720][vcodec!=none][acodec!=none]")
    command = [options.yt_dlp, "--ignore-config", "--no-playlist", "--no-warnings",
               "--socket-timeout", "15", "--retries", "1", "--no-progress",
               "--skip-download"]
    if options.js_runtime:
        # Keep runtime:path a single argv item; never run a shell command.
        command += ["--js-runtimes", options.js_runtime]
    return command + ["-f", selector, "-g", "--", url]


class BridgeServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, options):
        self.options = options
        self.busy = threading.Lock()
        self.stopping = threading.Event()
        self.active_lock = threading.Lock()
        self.active: set[subprocess.Popen] = set()
        super().__init__(address, Handler)

    def start_process(self, args):
        with self.active_lock:
            if self.stopping.is_set():
                raise BridgeError("El puente se está cerrando")
            process = launch(args)
            self.active.add(process)
        return process

    def stop_process(self, process):
        if process is not None:
            stop_tree(process)
            with self.active_lock:
                self.active.discard(process)

    def stop_children(self):
        self.stopping.set()
        with self.active_lock:
            children = list(self.active)
        for child in children:
            self.stop_process(child)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"  # EOF-framed MPEG-TS; VLC need not buffer to a size.
    server_version = "TubeLivianoBridge/0.5"

    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, fmt, *args):
        # Never log signed media URLs, query parameters, or access tokens.
        pass

    def answer(self, status, message):
        content = message.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(content)

    def disconnected(self):
        if self.server.stopping.is_set():
            return True
        try:
            readable, _, _ = select.select([self.connection], [], [], 0)
            return bool(readable) and self.connection.recv(1, socket.MSG_PEEK) == b""
        except (OSError, ValueError):
            return True

    def resolve(self, url):
        opts = self.server.options
        if opts.test_file:
            return [str(Path(opts.test_file).resolve())]
        process = self.server.start_process(resolver_command(opts, url))
        tail = bytearray()
        errors = threading.Thread(target=drain_stderr, args=(process.stderr, tail), daemon=True)
        errors.start()
        stopped = threading.Event()
        output = queue.Queue(maxsize=4)
        reader = threading.Thread(target=pipe_reader, args=(process.stdout, output, stopped), daemon=True)
        reader.start()
        data = bytearray()
        deadline = time.monotonic() + 50
        try:
            while True:
                if self.disconnected():
                    raise ConnectionAbortedError("Cliente desconectado")
                if time.monotonic() > deadline:
                    raise BridgeError("yt-dlp excedió 50 s. Reintente o actualice el motor del puente.")
                try:
                    chunk = output.get(timeout=0.2)
                except queue.Empty:
                    continue
                if chunk is None:
                    break
                data.extend(chunk)
                if len(data) > 65536:
                    raise BridgeError("La respuesta de yt-dlp superó el límite")
            try:
                result = process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                raise BridgeError("yt-dlp no terminó después de resolver")
            if result:
                suffix = " (HTTP 429)" if b"429" in tail else ""
                raise BridgeError("YouTube no permitió resolver el video" + suffix +
                                  ". Actualice yt-dlp; puede requerir verificación en la PC puente.")
            urls = data.decode("utf-8").strip().splitlines()
            if not 1 <= len(urls) <= 2 or any(
                not value.startswith(("http://", "https://")) or any(ord(c) < 32 for c in value)
                for value in urls
            ):
                raise BridgeError("yt-dlp devolvió streams no válidos")
            return urls
        finally:
            stopped.set()
            self.server.stop_process(process)
            reader.join(timeout=2)
            errors.join(timeout=2)

    def do_GET(self):
        try:
            self.get_request()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, TimeoutError):
            pass

    def get_request(self):
        parsed = urlsplit(self.path)
        query = parse_qs(parsed.query)
        opts = self.server.options
        if opts.token:
            token = query.get("token", [""])[0]
            if self.headers.get("Authorization", "").startswith("Bearer "):
                token = self.headers["Authorization"][7:]
            if not hmac.compare_digest(token.encode(), opts.token.encode()):
                return self.answer(401, "Token del puente incorrecto")
        if parsed.path == "/health":
            return self.answer(200, json.dumps({"app": "TubeLiviano", "version": "0.5",
                                               "heights": list(HEIGHTS), "busy": self.server.busy.locked()}))
        if parsed.path != "/play":
            return self.answer(404, "Use /play?url=URL_YOUTUBE&height=240")
        url = query.get("url", [""])[0]
        try:
            height = int(query.get("height", ["240"])[0])
        except ValueError:
            height = 0
        if height not in HEIGHTS:
            return self.answer(400, "Perfil permitido: 144, 240 o 360")
        if not youtube_url(url):
            return self.answer(400, "Se requiere una URL HTTP(S) de YouTube")
        if not self.server.busy.acquire(blocking=False):
            return self.answer(503, "El puente está ocupado. Cancele el otro video y reintente.")
        process = None
        stopped = threading.Event()
        reader = errors = None
        headers_sent = False
        try:
            sources = self.resolve(url)
            process = self.server.start_process(ffmpeg_command(opts.ffmpeg, sources, height))
            output = queue.Queue(maxsize=4)
            tail = bytearray()
            reader = threading.Thread(target=pipe_reader, args=(process.stdout, output, stopped), daemon=True)
            errors = threading.Thread(target=drain_stderr, args=(process.stderr, tail), daemon=True)
            reader.start()
            errors.start()
            last_data = time.monotonic()
            deadline = last_data + 4 * 3600
            while True:
                if self.disconnected():
                    raise ConnectionAbortedError("Cliente desconectado")
                if time.monotonic() - last_data > 30 or time.monotonic() > deadline:
                    raise BridgeError("FFmpeg no produjo datos o excedió el límite de 4 horas")
                try:
                    chunk = output.get(timeout=0.2)
                except queue.Empty:
                    continue
                if chunk is None:
                    if not headers_sent:
                        raise BridgeError("FFmpeg no produjo imagen. Compruebe libx264/AAC y la fuente.")
                    break
                if not headers_sent:
                    self.send_response(200)
                    self.send_header("Content-Type", "video/mp2t")
                    self.send_header("Cache-Control", "no-store")
                    self.send_header("Connection", "close")
                    self.send_header("X-TubeLiviano-Profile", str(height))
                    self.end_headers()
                    headers_sent = True
                self.wfile.write(chunk)
                self.wfile.flush()
                last_data = time.monotonic()
        except (BridgeError, FileNotFoundError) as exc:
            if not headers_sent:
                self.answer(502, str(exc))
        finally:
            stopped.set()
            self.server.stop_process(process)
            if reader:
                reader.join(timeout=2)
            if errors:
                errors.join(timeout=2)
            self.server.busy.release()
            self.close_connection = True


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--host", default="127.0.0.1", help="IP local; use su IP LAN para permitir la netbook")
    result.add_argument("--port", type=int, default=8787)
    result.add_argument("--yt-dlp", default="yt-dlp", help="Ruta al motor de la PC puente")
    result.add_argument("--js-runtime", help="Runtime de yt-dlp, por ejemplo quickjs:C:\\TubeLiviano\\runtimes\\qjs.exe; omitido usa el predeterminado de yt-dlp")
    result.add_argument("--ffmpeg", default="ffmpeg")
    result.add_argument("--token", default="", help="Token opcional para una LAN compartida")
    result.add_argument("--test-file", help="Solo pruebas offline: reemplaza YouTube por este clip local")
    return result


def main():
    options = parser().parse_args()
    if options.test_file and not Path(options.test_file).is_file():
        raise SystemExit("--test-file no existe")
    server = BridgeServer((options.host, options.port), options)
    print(f"TubeLiviano puente 0.5 en {options.host}:{server.server_port}", flush=True)
    if options.host not in ("127.0.0.1", "localhost", "::1"):
        print("Acceso LAN activado: no reenvíe este puerto a Internet. Configure el firewall local.", flush=True)
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.stop_children()
        server.server_close()


if __name__ == "__main__":
    main()
