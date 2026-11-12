#ifndef TUBELIVIANO_ENGINE_WORKER_H
#define TUBELIVIANO_ENGINE_WORKER_H
#include "engine.h"

typedef enum { ENGINE_WORKER_HANDLED, ENGINE_WORKER_UNAVAILABLE } EngineWorkerResult;
/* HANDLED: *output owns a process-heap allocation on success, otherwise failure
 * and error are set. UNAVAILABLE alone permits the legacy one-shot CLI path.
 * Only one caller is accepted; call from the extractor worker, never the GUI. */
EngineWorkerResult engine_worker_search(Engine *engine, const WCHAR *query,
    int limit, HANDLE cancel, DWORD timeout, char **output, WCHAR *error, size_t cap);
EngineWorkerResult engine_worker_resolve(Engine *engine, const WCHAR *url,
    int height, BOOL fallback, HANDLE cancel, DWORD timeout,
    char **output, WCHAR *error, size_t cap);
/* Asynchronous Cancel/Close; terminates the actor and its entire child Job.
 * A subsequent request may lazily start another actor. closed() only polls. */
void engine_worker_stop(void);
BOOL engine_worker_closed(void);
#endif
