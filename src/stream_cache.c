#include "stream_cache.h"
#include <string.h>
#include <wchar.h>

static BOOL quality_valid(int height)
{
    return height == 144 || height == 240 || height == 360;
}

static BOOL key_valid(const WCHAR *url, size_t *length)
{
    size_t n;
    if (!url) return FALSE;
    for (n = 0; n < STREAM_CACHE_KEY_CAP && url[n]; ++n)
        if (url[n] <= 32 || url[n] == 127) return FALSE;
    if (n == STREAM_CACHE_KEY_CAP ||
        !((n > 7 && !wmemcmp(url, L"http://", 7) && !wcschr(L"/?#", url[7])) ||
          (n > 8 && !wmemcmp(url, L"https://", 8) && !wcschr(L"/?#", url[8])))) return FALSE;
    if (length) *length = n;
    return TRUE;
}

static BOOL stream_valid(const char *url, BOOL optional)
{
    size_t n;
    if (!url) return FALSE;
    for (n = 0; n < ENGINE_URL_CAP && url[n]; ++n)
        if ((unsigned char)url[n] <= 32 || (unsigned char)url[n] == 127) return FALSE;
    if (!n) return optional;
    return n < ENGINE_URL_CAP &&
        ((n > 7 && !memcmp(url, "http://", 7) && !strchr("/?#", url[7])) ||
         (n > 8 && !memcmp(url, "https://", 8) && !strchr("/?#", url[8])));
}

static BOOL expired(const StreamCacheEntry *entry, DWORD now)
{
    return (DWORD)(now - entry->inserted) >= STREAM_CACHE_TTL_MS;
}

void stream_cache_clear(StreamCache *cache)
{
    if (cache) memset(cache, 0, sizeof(*cache));
}

void stream_cache_init(StreamCache *cache)
{
    stream_cache_clear(cache);
}

BOOL stream_cache_get(StreamCache *cache, const WCHAR *url, int height,
                      DWORD now, EngineStreams *out)
{
    int i;
    if (!cache || !out || !quality_valid(height) || !key_valid(url, NULL)) {
        if (out) memset(out, 0, sizeof(*out));
        return FALSE;
    }
    for (i = 0; i < STREAM_CACHE_ENTRIES; ++i) {
        StreamCacheEntry *entry = &cache->entries[i];
        if (!entry->valid) continue;
        if (expired(entry, now)) {
            memset(entry, 0, sizeof(*entry));
            continue;
        }
        if (entry->height != height || wcscmp(entry->url, url)) continue;
        memmove(out, &entry->streams, sizeof(*out));
        entry->last_used = now;
        return TRUE;
    }
    memset(out, 0, sizeof(*out));
    return FALSE;
}

void stream_cache_put(StreamCache *cache, const WCHAR *url, int height,
                      DWORD now, const EngineStreams *streams)
{
    int i, selected = -1;
    size_t length;
    StreamCacheEntry *entry;
    if (!cache || !streams || !quality_valid(height) || !key_valid(url, &length) ||
        !stream_valid(streams->video, FALSE) || !stream_valid(streams->audio, TRUE)) return;
    /* Replacing a key refreshes its lifetime only after a new resolution. */
    for (i = 0; i < STREAM_CACHE_ENTRIES; ++i)
        if (cache->entries[i].valid && cache->entries[i].height == height &&
            !wcscmp(cache->entries[i].url, url)) { selected = i; break; }
    if (selected < 0)
        for (i = 0; i < STREAM_CACHE_ENTRIES; ++i)
            if (!cache->entries[i].valid || expired(&cache->entries[i], now)) { selected = i; break; }
    if (selected < 0) {
        selected = 0;
        for (i = 1; i < STREAM_CACHE_ENTRIES; ++i)
            if ((DWORD)(now - cache->entries[i].last_used) >
                (DWORD)(now - cache->entries[selected].last_used)) selected = i;
    }
    entry = &cache->entries[selected];
    /* memmove also supports replacing an entry using its own stored streams. */
    memmove(entry->url, url, (length + 1) * sizeof(*url));
    memset(entry->url + length + 1, 0,
           (STREAM_CACHE_KEY_CAP - length - 1) * sizeof(*url));
    memmove(&entry->streams, streams, sizeof(*streams));
    entry->height = height;
    entry->inserted = entry->last_used = now;
    entry->valid = TRUE;
}

void stream_cache_remove(StreamCache *cache, const WCHAR *url, int height)
{
    int i;
    if (!cache || !quality_valid(height) || !key_valid(url, NULL)) return;
    for (i = 0; i < STREAM_CACHE_ENTRIES; ++i) {
        StreamCacheEntry *entry = &cache->entries[i];
        if (entry->valid && entry->height == height && !wcscmp(entry->url, url))
            memset(entry, 0, sizeof(*entry));
    }
}
