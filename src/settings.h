#ifndef TUBELIVIANO_SETTINGS_H
#define TUBELIVIANO_SETTINGS_H
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#include <stddef.h>

typedef struct {
    int quality;              /* 144, 240 (Atom default), 360 */
    int cache_ms;             /* 250..3000, default 450 */
    int prefetch_kib;         /* 64, 128 (default), 256 */
    int decoder_threads;      /* 1 (default), 2 */
    int volume;               /* 0..100, default 80 */
    BOOL muted;
    BOOL dark_theme;          /* TRUE by default */
    BOOL large_text;
    int results_limit;        /* 6 or 12 (default) */
    int search_timeout_s;     /* 30, 60, 120 (default) */
    int resolve_timeout_s;    /* 30, 60, 90 (default) */
    int mode;                 /* 0 direct, 1 bridge */
    BOOL remember_size;
    int width, height;        /* normal outer window size */
    WCHAR bridge_url[256];    /* query token is session-only, never written */
} AppSettings;

void settings_defaults(AppSettings *settings);
void settings_validate(AppSettings *settings);
BOOL settings_load(AppSettings *settings, const WCHAR *base, WCHAR *notice, size_t cap);
BOOL settings_save(const AppSettings *settings, const WCHAR *base, WCHAR *notice, size_t cap);
/* TRUE means apply the returned settings to this session, including when a
 * read-only portable folder prevented saving. Cancel leaves settings unchanged. */
BOOL settings_dialog(HWND owner, AppSettings *settings, const WCHAR *base,
                     WCHAR *notice, size_t cap);
#endif
