/* Bounded stream-cache regressions; no network, codecs or OS execution needed.
 * Native ASan/UBSan: cc -DSTREAM_CACHE_NATIVE -std=c99 -Wall -Wextra -Werror \
 *   -fsanitize=address,undefined tests/stream_cache_test.c -o build/cache_native
 * Win32: i686-w64-mingw32-gcc -std=c99 -Os -Wall -Wextra -Werror \
 *   tests/stream_cache_test.c src/stream_cache.c -o build/stream_cache_test.exe
 * The native harness supplies only the Windows scalar/streams declarations;
 * the included implementation is the exact production stream_cache.c. */
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#ifdef STREAM_CACHE_NATIVE
#include <stdint.h>
typedef uint32_t DWORD;
typedef wchar_t WCHAR;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define TUBELIVIANO_ENGINE_H
#define ENGINE_URL_CAP 16384
typedef struct { char video[ENGINE_URL_CAP]; char audio[ENGINE_URL_CAP]; } EngineStreams;
#include "../src/stream_cache.c"
#else
#include "../src/stream_cache.h"
#endif

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static int checks, failures;
static StreamCache cache, other, snapshot;
static EngineStreams muxed, split, out, bad;
static const WCHAR first[] = L"https://www.youtube.com/watch?v=abcdefghijk";
static const WCHAR second[] = L"https://youtu.be/0123456789_";
static const WCHAR third[] = L"https://www.youtube.com/watch?v=zyxwvutsrqp";
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %d: %s\n", __LINE__, #c); } } while (0)

static void defaults(void)
{
    stream_cache_init(&cache);
    stream_cache_init(&other);
    memset(&muxed, 0, sizeof(muxed)); memset(&split, 0, sizeof(split));
    strcpy(muxed.video, "https://cdn.invalid/video.mp4?expire=123&sig=SIGNED%2BVALUE");
    strcpy(split.video, "https://cdn.invalid/video-only.mp4?expire=123&sig=VIDEO");
    strcpy(split.audio, "http://cdn.invalid/audio.m4a?expire=123&sig=AUDIO");
}
static int entries(const StreamCache *c)
{
    int i, count = 0;
    for (i = 0; i < STREAM_CACHE_ENTRIES; ++i) if (c->entries[i].valid) ++count;
    return count;
}
static BOOL empty_output(void)
{
    static EngineStreams empty;
    return !memcmp(&out, &empty, sizeof(out));
}
static void basic_and_copies(void)
{
    defaults(); memset(&out, 0x7f, sizeof(out));
    CHECK(!stream_cache_get(&cache, first, 240, 0, &out)); CHECK(empty_output());
    stream_cache_put(&cache, first, 240, 100, &muxed);
    CHECK(entries(&cache) == 1);
    CHECK(stream_cache_get(&cache, first, 240, 100, &out));
    CHECK(!memcmp(&out, &muxed, sizeof(out))); CHECK(!out.audio[0]);
    strcpy(out.video, "https://changed.invalid/out");
    CHECK(stream_cache_get(&cache, first, 240, 101, &out));
    CHECK(!strcmp(out.video, muxed.video));
    strcpy(muxed.video, "https://changed.invalid/input");
    CHECK(stream_cache_get(&cache, first, 240, 102, &out));
    CHECK(!strcmp(out.video, "https://cdn.invalid/video.mp4?expire=123&sig=SIGNED%2BVALUE"));
    stream_cache_put(&cache, second, 144, 103, &split);
    CHECK(stream_cache_get(&cache, second, 144, 104, &out));
    CHECK(!memcmp(&out, &split, sizeof(out)));
    CHECK(!stream_cache_get(&cache, second, 240, 104, &out)); CHECK(empty_output());
    CHECK(!stream_cache_get(&cache, L"https://youtu.be/0123456789_?t=1", 144, 104, &out));
    CHECK(!stream_cache_get(&cache, L"http://youtu.be/0123456789_", 144, 104, &out));
}
static void expiry(void)
{
    DWORD start = (DWORD)0xfffffff0u, end = (DWORD)(start + STREAM_CACHE_TTL_MS);
    defaults();
    stream_cache_put(&cache, first, 240, 100, &muxed);
    CHECK(stream_cache_get(&cache, first, 240, 120099, &out));
    CHECK(!stream_cache_get(&cache, first, 240, 120100, &out));
    CHECK(empty_output()); CHECK(entries(&cache) == 0);
    stream_cache_put(&cache, first, 240, 0, &muxed);
    CHECK(stream_cache_get(&cache, first, 240, 119999, &out));
    CHECK(!stream_cache_get(&cache, first, 240, 120000, &out)); /* A hit did not extend TTL. */
    stream_cache_put(&cache, first, 240, start, &split);
    CHECK(stream_cache_get(&cache, first, 240, 20, &out));
    CHECK(!memcmp(&out, &split, sizeof(out)));
    CHECK(stream_cache_get(&cache, first, 240, (DWORD)(end - 1), &out));
    CHECK(!stream_cache_get(&cache, first, 240, end, &out));
    CHECK(entries(&cache) == 0);
}
static void replacement_and_eviction(void)
{
    defaults();
    stream_cache_put(&cache, first, 240, 0, &muxed);
    stream_cache_put(&cache, second, 144, 10, &split);
    CHECK(stream_cache_get(&cache, first, 240, 20, &out));
    stream_cache_put(&cache, third, 360, 30, &muxed);
    CHECK(entries(&cache) == 2);
    CHECK(!stream_cache_get(&cache, second, 144, 30, &out));
    CHECK(stream_cache_get(&cache, first, 240, 30, &out));
    CHECK(stream_cache_get(&cache, third, 360, 30, &out));
    stream_cache_put(&cache, first, 240, 100, &split);
    CHECK(entries(&cache) == 2);
    CHECK(stream_cache_get(&cache, first, 240, 120099, &out));
    CHECK(!memcmp(&out, &split, sizeof(out)));
    CHECK(!stream_cache_get(&cache, first, 240, 120100, &out));

    defaults();
    stream_cache_put(&cache, first, 240, 0, &muxed);
    stream_cache_put(&cache, second, 144, 50000, &split);
    CHECK(stream_cache_get(&cache, first, 240, 119999, &out));
    stream_cache_put(&cache, third, 360, 120001, &muxed); /* Expired wins over LRU. */
    CHECK(!stream_cache_get(&cache, first, 240, 120001, &out));
    CHECK(stream_cache_get(&cache, second, 144, 120001, &out));
    CHECK(stream_cache_get(&cache, third, 360, 120001, &out));

    defaults();
    stream_cache_put(&cache, first, 240, (DWORD)0xffffff00u, &muxed);
    stream_cache_put(&cache, second, 144, (DWORD)0xffffff80u, &split);
    CHECK(stream_cache_get(&cache, first, 240, 10, &out));
    stream_cache_put(&cache, third, 360, 20, &muxed);
    CHECK(!stream_cache_get(&cache, second, 144, 21, &out));
    CHECK(stream_cache_get(&cache, first, 240, 21, &out));
    CHECK(stream_cache_get(&cache, third, 360, 21, &out));
}
static void invalidation_and_instances(void)
{
    defaults();
    stream_cache_put(&cache, first, 144, 0, &muxed);
    stream_cache_put(&cache, first, 240, 0, &split);
    stream_cache_put(&other, third, 360, 0, &muxed);
    stream_cache_remove(&cache, first, 144);
    CHECK(entries(&cache) == 1);
    CHECK(!stream_cache_get(&cache, first, 144, 1, &out));
    CHECK(stream_cache_get(&cache, first, 240, 1, &out));
    stream_cache_remove(&cache, first, 360); stream_cache_remove(&cache, third, 240);
    CHECK(stream_cache_get(&cache, first, 240, 2, &out));
    stream_cache_clear(&cache); CHECK(entries(&cache) == 0);
    CHECK(!stream_cache_get(&cache, first, 240, 3, &out));
    CHECK(stream_cache_get(&other, third, 360, 3, &out));
    stream_cache_init(&other); CHECK(entries(&other) == 0);
    stream_cache_clear(NULL); stream_cache_init(NULL);
    stream_cache_remove(NULL, first, 240); stream_cache_remove(&cache, NULL, 240);
    CHECK(entries(&cache) == 0);
}
static void rejected_put(const WCHAR *key, int quality, const EngineStreams *streams)
{
    snapshot = cache;
    stream_cache_put(&cache, key, quality, 10, streams);
    CHECK(!memcmp(&cache, &snapshot, sizeof(cache)));
}
static void invalid_inputs(void)
{
    static WCHAR long_key[STREAM_CACHE_KEY_CAP + 1];
    const WCHAR *keys[] = {NULL, L"", L"ftp://cdn.invalid/file", L"https://", L"http://",
        L"https:///missing-host", L"http://?query", L"http://#fragment",
        L"HTTPS://cdn.invalid/file", L"https://cdn.invalid/a b", L"https://cdn.invalid/a\n"};
    const char *urls[] = {"", "file:///tmp/video.mp4", "ftp://cdn.invalid/file", "https://", "http://",
        "https:///missing-host", "http://?query", "http://#fragment",
        "HTTPS://cdn.invalid/file", "https://cdn.invalid/a b", "https://cdn.invalid/a\n",
        "https://cdn.invalid/a\x7f"};
    size_t i;
    defaults(); stream_cache_put(&cache, first, 240, 0, &muxed);
    for (i = 0; i < COUNT(keys); ++i) rejected_put(keys[i], 240, &muxed);
    rejected_put(first, 0, &muxed); rejected_put(first, 720, &muxed); rejected_put(first, 240, NULL);
    for (i = 0; i < COUNT(urls); ++i) {
        bad = muxed; strcpy(bad.video, urls[i]); rejected_put(first, 240, &bad);
        if (urls[i][0]) { bad = split; strcpy(bad.audio, urls[i]); rejected_put(first, 240, &bad); }
    }
    bad = muxed; memset(bad.video, 'a', sizeof(bad.video)); memcpy(bad.video, "https://", 8);
    rejected_put(first, 240, &bad);
    bad = split; memset(bad.audio, 'a', sizeof(bad.audio)); memcpy(bad.audio, "https://", 8);
    rejected_put(first, 240, &bad);
    for (i = 0; i < STREAM_CACHE_KEY_CAP; ++i) long_key[i] = L'a';
    memcpy(long_key, L"https://", 8 * sizeof(*long_key)); long_key[STREAM_CACHE_KEY_CAP] = 0;
    rejected_put(long_key, 240, &muxed); /* NUL beyond cap is still rejected. */
    stream_cache_put(NULL, first, 240, 0, &muxed);
    memset(&out, 0x55, sizeof(out)); CHECK(!stream_cache_get(NULL, first, 240, 0, &out)); CHECK(empty_output());
    CHECK(!stream_cache_get(&cache, NULL, 240, 0, &out)); CHECK(empty_output());
    CHECK(!stream_cache_get(&cache, first, 720, 0, &out)); CHECK(empty_output());
    CHECK(!stream_cache_get(&cache, first, 240, 0, NULL));
    CHECK(stream_cache_get(&cache, first, 240, 0, &out));
}
static void cap_boundaries(void)
{
    static WCHAR key[STREAM_CACHE_KEY_CAP];
    size_t i;
    defaults();
    for (i = 0; i < STREAM_CACHE_KEY_CAP - 1; ++i) key[i] = L'a';
    memcpy(key, L"https://", 8 * sizeof(*key)); key[STREAM_CACHE_KEY_CAP - 1] = 0;
    memset(&bad, 'x', sizeof(bad)); memcpy(bad.video, "https://", 8); memcpy(bad.audio, "http://", 7);
    bad.video[ENGINE_URL_CAP - 1] = bad.audio[ENGINE_URL_CAP - 1] = 0;
    stream_cache_put(&cache, key, 360, 0, &bad);
    CHECK(stream_cache_get(&cache, key, 360, 1, &out)); CHECK(!memcmp(&bad, &out, sizeof(out)));
    stream_cache_put(&cache, L"https://youtu.be/caf\u00e9", 144, 0, &muxed);
    CHECK(stream_cache_get(&cache, L"https://youtu.be/caf\u00e9", 144, 1, &out));
    CHECK(!memcmp(&muxed, &out, sizeof(out)));
    CHECK(sizeof(StreamCache) < 110 * 1024);
}
int main(void)
{
    basic_and_copies(); expiry(); replacement_and_eviction(); invalidation_and_instances(); invalid_inputs(); cap_boundaries();
    printf("Stream cache: %d checks; %d failures (memory-only, no network/media claim)\n", checks, failures);
    return failures ? 1 : 0;
}
