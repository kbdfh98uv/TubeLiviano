#ifndef TUBELIVIANO_ENGINE_H
#define TUBELIVIANO_ENGINE_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#include <stddef.h>

#define ENGINE_MAX_RESULTS 12
#define ENGINE_URL_CAP 16384

typedef enum {
    ENGINE_FAILURE_NONE = 0,
    ENGINE_FAILURE_EXTRACT, /* Normal extractor exit 1: format probing may help. */
    ENGINE_FAILURE_LOADER,
    ENGINE_FAILURE_TIMEOUT,
    ENGINE_FAILURE_OUTPUT,
    ENGINE_FAILURE_IO,
    ENGINE_FAILURE_CANCELLED,
    ENGINE_FAILURE_ARGUMENT,
    ENGINE_FAILURE_PROCESS
} EngineFailure;

typedef struct {
    WCHAR exe[MAX_PATH];
    WCHAR base[MAX_PATH];
    BOOL xp;
    DWORD os_major, os_minor, os_build;
    EngineFailure failure;
    /* Per-request settings; zero preserves the portable factory defaults. */
    int search_limit;       /* 6 or 12 results (default 12). */
    int search_timeout_ms;  /* 30000, 60000 or 120000 (default 120000). */
    int resolve_timeout_ms; /* 30000, 60000 or 90000 (default 90000). */
} Engine;

/* Only a normal extractor failure can benefit from retrying format probing. */
BOOL engine_format_probe_allowed(const Engine *engine);

typedef struct {
    WCHAR title[512];
    WCHAR channel[256];
    WCHAR id[32];
    int duration; /* Seconds; -1 means unavailable (e.g. live video). */
} EngineResult;

typedef struct {
    char video[ENGINE_URL_CAP]; /* UTF-8 HTTP(S) URL. */
    char audio[ENGINE_URL_CAP]; /* Empty for a multiplexed stream. */
} EngineStreams;

/* All error capacities are measured in WCHARs, including the terminator.
 * Call search/resolve on a worker. cancel is a caller-owned manual-reset event.
 * Cancelling or timing out terminates the extractor AND its descendants.
 * These calls never access the UI or retain pointers after returning. */
BOOL engine_init(Engine *engine, const WCHAR *base, WCHAR *error, size_t cap);
BOOL engine_search(Engine *engine, const WCHAR *query, HANDLE cancel,
                   EngineResult results[ENGINE_MAX_RESULTS], int *count,
                   WCHAR *error, size_t cap);
BOOL engine_resolve(Engine *engine, const WCHAR *url, int height, BOOL fallback,
                    HANDLE cancel, EngineStreams *streams,
                    WCHAR *error, size_t cap);

#endif
