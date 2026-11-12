"""TubeLiviano's bounded stdin worker, loaded by yt-dlp's public plugin API.

The packaged interpreter stays alive briefly after search, then exits after
resolve. No media is downloaded and no private PyInstaller API is used. MIT.
"""
import itertools
import json
import os
import queue
import sys
import threading

from yt_dlp.extractor.common import InfoExtractor

MAX_REQUEST = 65536
MAX_OUTPUT = 1024 * 1024
IDLE_SECONDS = 15


def frame(kind, value):
    data = value.encode('utf-8')
    if len(data) > MAX_OUTPUT or b'\0' in data:
        raise ValueError('respuesta demasiado grande o invalida')
    output = sys.stdout.buffer
    output.write(('TLW1 %s %d\n' % (kind, len(data))).encode('ascii'))
    output.write(data + b'\n')
    output.flush()


def read_one(result):
    try:
        result.put(sys.stdin.buffer.readline(MAX_REQUEST + 1))
    except Exception as error:
        result.put(error)


class TubeLivianoWorkerIE(InfoExtractor):
    IE_NAME = 'tubeliviano:worker'
    _VALID_URL = r'tubeliviano:worker$'

    def _real_extract(self, url):
        sys.stdout.buffer.write(b'TLW1 READY\n')
        sys.stdout.buffer.flush()
        ydl = self._downloader
        while True:
            result = queue.Queue(maxsize=1)
            reader = threading.Thread(target=read_one, args=(result,), daemon=True)
            reader.start()
            try:
                line = result.get(timeout=IDLE_SECONDS)
            except queue.Empty:
                # A daemon may be blocked in stdin. Exit only the Python child;
                # the unmodified one-file parent performs its normal cleanup.
                os._exit(0)
            reader.join()
            if not line:
                break
            operation = None
            try:
                if isinstance(line, Exception):
                    raise line
                if len(line) > MAX_REQUEST or not line.endswith(b'\n'):
                    raise ValueError('peticion demasiado larga')
                request = json.loads(line.decode('utf-8'))
                operation = request.get('op')
                if operation == 'close':
                    break
                source = request.get('input')
                if not isinstance(source, str) or not source or '\0' in source:
                    raise ValueError('consulta o enlace invalido')
                if operation == 'search':
                    limit = request.get('limit')
                    if limit not in (6, 12) or len(source) > 2048:
                        raise ValueError('limite de busqueda invalido')
                    ydl.params['extract_flat'] = 'in_playlist'
                    info = ydl.extract_info('ytsearch%d:%s' % (limit, source), download=False)
                    entries = itertools.islice(info.get('entries') or [], limit)
                    payload = ''.join(json.dumps({key: item.get(key) for key in
                        ('id', 'title', 'channel', 'uploader', 'duration')},
                        ensure_ascii=True, separators=(',', ':')) + '\n'
                        for item in entries if item)
                elif operation == 'resolve':
                    height = request.get('height')
                    if height not in (144, 240, 360) or len(source) > 8192 or not source.startswith(('https://', 'http://')):
                        raise ValueError('perfil o enlace HTTP invalido')
                    selector = (
                        'best[height<=%d][fps<=30][vcodec^=avc1][acodec^=mp4a]/'
                        'bestvideo[height<=%d][fps<=30][vcodec^=avc1]+bestaudio[acodec^=mp4a]'
                    ) % (height, height)
                    ydl.params['extract_flat'] = False
                    ydl.params['check_formats'] = request.get('fallback') is True
                    ydl.params['format'] = selector
                    # YoutubeDL compiles its selector at construction time.
                    # Changing params alone would silently use its HD default.
                    ydl.format_selector = ydl.build_format_selector(selector)
                    info = ydl.extract_info(source, download=False)
                    formats = info.get('requested_formats') or [info]
                    urls = [item['url'] for item in formats]
                    if not 1 <= len(urls) <= 2 or any(not isinstance(item, str) or
                            not item.startswith(('http://', 'https://')) or len(item) >= 16384 or
                            any(ord(c) <= 32 or ord(c) == 127 for c in item) for item in urls):
                        raise ValueError('el motor no devolvio uno o dos streams HTTP validos')
                    payload = ''.join(item + '\n' for item in urls)
                else:
                    raise ValueError('operacion invalida')
                frame('OK', payload)
            except Exception as error:
                frame('ERROR', str(error)[:1800])
                break
            if operation == 'resolve':
                break
        return self.playlist_result([], playlist_id='tubeliviano-worker')
