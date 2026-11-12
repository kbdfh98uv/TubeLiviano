#ifndef TUBELIVIANO_STREAM_CACHE_H
#define TUBELIVIANO_STREAM_CACHE_H
#include "engine.h"

#define STREAM_CACHE_ENTRIES 2
#define STREAM_CACHE_KEY_CAP 2048
#define STREAM_CACHE_TTL_MS 120000u

typedef struct {
    BOOL valid;
    DWORD inserted, last_used;
    int height;
    WCHAR url[STREAM_CACHE_KEY_CAP];
    EngineStreams streams;
} StreamCacheEntry;

typedef struct {
    StreamCacheEntry entries[STREAM_CACHE_ENTRIES];
} StreamCache;

/* UI-thread owned, in-memory only. Call put only after newly resolved streams
 * have produced image and audio. get updates LRU order, never the expiry time.
 * Explicit Retry and format probing must bypass get; invalidate a failed hit.
 * now is GetTickCount() (unsigned elapsed arithmetic handles a tick rollover). */
void stream_cache_init(StreamCache *cache);
void stream_cache_clear(StreamCache *cache);
BOOL stream_cache_get(StreamCache *cache, const WCHAR *url, int height,
                      DWORD now, EngineStreams *out);
void stream_cache_put(StreamCache *cache, const WCHAR *url, int height,
                      DWORD now, const EngineStreams *streams);
void stream_cache_remove(StreamCache *cache, const WCHAR *url, int height);
#endif
