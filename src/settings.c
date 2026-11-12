#include "settings.h"
#include "ui.h"
#include <limits.h>
#include <wchar.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct {
    int quality, cache_ms, prefetch_kib, decoder_threads, mode;
    const WCHAR *name, *description;
} SettingsPreset;

static const SettingsPreset presets[PRESET_COUNT] = {
    {144, 250, 64, 1, 0, L"Mínimo · 144p",
     L"144p · 250 ms · 64 KiB · 1 hilo · Directo"},
    {240, 450, 128, 1, 0, L"Atom · 240p (recomendado)",
     L"240p · 450 ms · 128 KiB · 1 hilo · Directo"},
    {240, 1500, 256, 1, 0, L"Conexión inestable · 240p",
     L"240p · 1500 ms · 256 KiB · 1 hilo · Directo"},
    {360, 600, 128, 2, 0, L"360p · Dos hilos",
     L"360p · 600 ms · 128 KiB · 2 hilos · Directo"},
    {240, 800, 128, 1, 1, L"Puente · 240p",
     L"240p · 800 ms · 128 KiB · 1 hilo · Puente"}
};

BOOL settings_apply_preset(AppSettings *s, int preset) {
    const SettingsPreset *p;
    if (!s || preset < 0 || preset >= PRESET_COUNT) return FALSE;
    p = &presets[preset];
    s->quality = p->quality;
    s->cache_ms = p->cache_ms;
    s->prefetch_kib = p->prefetch_kib;
    s->decoder_threads = p->decoder_threads;
    s->mode = p->mode;
    return TRUE;
}

static void copy_text(WCHAR *out, size_t cap, const WCHAR *text) {
    size_t n;
    if (!out || !cap) return;
    n = wcslen(text);
    if (n >= cap) n = cap - 1;
    if (n) CopyMemory(out, text, n * sizeof(WCHAR));
    out[n] = 0;
}

static void set_notice(WCHAR *notice, size_t cap, const WCHAR *text) {
    copy_text(notice, cap, text);
}

void settings_defaults(AppSettings *s) {
    if (!s) return;
    ZeroMemory(s, sizeof(*s));
    s->quality = 240;
    s->cache_ms = 450;
    s->prefetch_kib = 128;
    s->decoder_threads = 1;
    s->volume = 80;
    s->dark_theme = TRUE;
    s->results_limit = 12;
    s->search_timeout_s = 120;
    s->resolve_timeout_s = 90;
    s->remember_size = TRUE;
    s->width = 840;
    s->height = 590;
    copy_text(s->bridge_url, COUNT(s->bridge_url), L"http://192.168.1.10:8787");
}

void settings_validate(AppSettings *s) {
    AppSettings d;
    if (!s) return;
    settings_defaults(&d);
    if (s->quality != 144 && s->quality != 240 && s->quality != 360) s->quality = d.quality;
    if (s->cache_ms < 250 || s->cache_ms > 3000) s->cache_ms = d.cache_ms;
    if (s->prefetch_kib != 64 && s->prefetch_kib != 128 && s->prefetch_kib != 256) s->prefetch_kib = d.prefetch_kib;
    if (s->decoder_threads != 1 && s->decoder_threads != 2) s->decoder_threads = d.decoder_threads;
    if (s->volume < 0 || s->volume > 100) s->volume = d.volume;
    if (s->muted != FALSE && s->muted != TRUE) s->muted = d.muted;
    if (s->dark_theme != FALSE && s->dark_theme != TRUE) s->dark_theme = d.dark_theme;
    if (s->large_text != FALSE && s->large_text != TRUE) s->large_text = d.large_text;
    if (s->results_limit != 6 && s->results_limit != 12) s->results_limit = d.results_limit;
    if (s->search_timeout_s != 30 && s->search_timeout_s != 60 && s->search_timeout_s != 120) s->search_timeout_s = d.search_timeout_s;
    if (s->resolve_timeout_s != 30 && s->resolve_timeout_s != 60 && s->resolve_timeout_s != 90) s->resolve_timeout_s = d.resolve_timeout_s;
    if (s->mode != 0 && s->mode != 1) s->mode = d.mode;
    if (s->remember_size != FALSE && s->remember_size != TRUE) s->remember_size = d.remember_size;
    if (s->width < 640 || s->width > 4096) s->width = d.width;
    if (s->height < 400 || s->height > 2160) s->height = d.height;
    s->bridge_url[COUNT(s->bridge_url) - 1] = 0;
}

static BOOL settings_paths(const WCHAR *base, WCHAR *folder, WCHAR *path) {
    DWORD length, attrs;
    size_t n;
    if (!base || !*base) return FALSE;
    length = GetFullPathNameW(base, MAX_PATH, folder, NULL);
    if (!length || length >= MAX_PATH) return FALSE;
    attrs = GetFileAttributesW(folder);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
    n = wcslen(folder);
    if (n && folder[n - 1] != L'\\') {
        if (n + 1 >= MAX_PATH) return FALSE;
        folder[n++] = L'\\';
        folder[n] = 0;
    }
    if (n + wcslen(L"TubeLiviano.ini") >= MAX_PATH) return FALSE;
    copy_text(path, MAX_PATH, folder);
    lstrcatW(path, L"TubeLiviano.ini");
    return TRUE;
}

static int parse_number(const WCHAR *value, int fallback) {
    unsigned int number = 0;
    const WCHAR *p = value;
    if (!*p) return fallback;
    while (*p) {
        unsigned int digit;
        if (*p < L'0' || *p > L'9') return fallback;
        digit = (unsigned int)(*p - L'0');
        if (number > ((unsigned int)INT_MAX - digit) / 10U) return fallback;
        number = number * 10U + digit;
        ++p;
    }
    return (int)number;
}

static int read_number(const WCHAR *path, const WCHAR *section, const WCHAR *key, int fallback) {
    WCHAR value[48];
    DWORD n = GetPrivateProfileStringW(section, key, L"", value, COUNT(value), path);
    if (!n || n >= COUNT(value) - 1) return fallback;
    return parse_number(value, fallback);
}

static void remove_query(WCHAR *url) {
    WCHAR *query = wcschr(url, L'?');
    if (query) *query = 0;
}

BOOL settings_load(AppSettings *s, const WCHAR *base, WCHAR *notice, size_t cap) {
    WCHAR folder[MAX_PATH], path[MAX_PATH];
    DWORD attrs, n;
    if (!s) return FALSE;
    settings_defaults(s);
    set_notice(notice, cap, L"");
    if (!settings_paths(base, folder, path)) {
        set_notice(notice, cap, L"No se pudo leer la carpeta portable. Se usan los valores predeterminados.");
        return FALSE;
    }
    attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return TRUE;
        set_notice(notice, cap, L"No se pudo leer TubeLiviano.ini. Se usan los valores predeterminados.");
        return FALSE;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        set_notice(notice, cap, L"TubeLiviano.ini no es un archivo. Se usan los valores predeterminados.");
        return FALSE;
    }
#define READ(section, key, member) s->member = read_number(path, L##section, L##key, s->member)
    READ("General", "DarkTheme", dark_theme);
    READ("General", "LargeText", large_text);
    READ("General", "ResultsLimit", results_limit);
    READ("General", "RememberSize", remember_size);
    READ("General", "Width", width);
    READ("General", "Height", height);
    READ("Playback", "Quality", quality);
    READ("Playback", "CacheMs", cache_ms);
    READ("Playback", "PrefetchKiB", prefetch_kib);
    READ("Playback", "DecoderThreads", decoder_threads);
    READ("Playback", "Volume", volume);
    READ("Playback", "Muted", muted);
    READ("Network", "SearchTimeout", search_timeout_s);
    READ("Network", "ResolveTimeout", resolve_timeout_s);
    READ("Network", "Mode", mode);
#undef READ
    {
        WCHAR bridge[COUNT(s->bridge_url)];
        n = GetPrivateProfileStringW(L"Network", L"BridgeURL", s->bridge_url,
                                     bridge, COUNT(bridge), path);
        copy_text(s->bridge_url, COUNT(s->bridge_url), bridge);
    }
    if (n >= COUNT(s->bridge_url) - 1) {
        AppSettings defaults;
        settings_defaults(&defaults);
        copy_text(s->bridge_url, COUNT(s->bridge_url), defaults.bridge_url);
    }
    remove_query(s->bridge_url);
    settings_validate(s);
    return TRUE;
}

static BOOL write_number(const WCHAR *path, const WCHAR *section, const WCHAR *key, int value) {
    WCHAR text[16];
    wsprintfW(text, L"%d", value);
    return WritePrivateProfileStringW(section, key, text, path);
}

BOOL settings_save(const AppSettings *settings, const WCHAR *base, WCHAR *notice, size_t cap) {
    WCHAR folder[MAX_PATH], path[MAX_PATH], temp[MAX_PATH];
    AppSettings s;
    DWORD attrs;
    BOOL ok = FALSE;
    if (!settings) return FALSE;
    s = *settings;
    settings_validate(&s);
    remove_query(s.bridge_url);
    set_notice(notice, cap, L"");
    if (!settings_paths(base, folder, path)) goto failed;
    attrs = GetFileAttributesW(path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))) goto failed;
    if (!GetTempFileNameW(folder, L"tli", 0, temp)) goto failed;
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!CopyFileW(path, temp, FALSE)) goto clean;
    } else {
        static const WORD bom = 0xfeff;
        DWORD written;
        HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) goto clean;
        ok = WriteFile(file, &bom, sizeof(bom), &written, NULL) && written == sizeof(bom);
        if (!CloseHandle(file)) ok = FALSE;
        if (!ok) goto clean;
    }
#define WRITE(section, key, member) if (!write_number(temp, L##section, L##key, s.member)) goto clean
    WRITE("General", "DarkTheme", dark_theme);
    WRITE("General", "LargeText", large_text);
    WRITE("General", "ResultsLimit", results_limit);
    WRITE("General", "RememberSize", remember_size);
    WRITE("General", "Width", width);
    WRITE("General", "Height", height);
    WRITE("Playback", "Quality", quality);
    WRITE("Playback", "CacheMs", cache_ms);
    WRITE("Playback", "PrefetchKiB", prefetch_kib);
    WRITE("Playback", "DecoderThreads", decoder_threads);
    WRITE("Playback", "Volume", volume);
    WRITE("Playback", "Muted", muted);
    WRITE("Network", "SearchTimeout", search_timeout_s);
    WRITE("Network", "ResolveTimeout", resolve_timeout_s);
    WRITE("Network", "Mode", mode);
#undef WRITE
    if (!WritePrivateProfileStringW(L"Network", L"BridgeURL", s.bridge_url, temp)) goto clean;
    /* The cache-flush form may return zero even after a successful flush. */
    WritePrivateProfileStringW(NULL, NULL, NULL, temp);
    {
        HANDLE file = CreateFileW(temp, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE) goto clean;
        ok = FlushFileBuffers(file);
        if (!CloseHandle(file)) ok = FALSE;
        if (!ok) goto clean;
    }
    ok = MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (ok) {
        /* Discard a cached view of the replaced portable INI. */
        WritePrivateProfileStringW(NULL, NULL, NULL, path);
        return TRUE;
    }
clean:
    DeleteFileW(temp);
failed:
    set_notice(notice, cap, L"Cambios aplicados a esta sesión. No se pudo guardar TubeLiviano.ini en la carpeta portable.");
    return FALSE;
}

enum {
    SD_GENERAL = 500, SD_PLAYBACK, SD_NETWORK, SD_DEFAULTS, SD_ATOM, SD_MINIMUM,
    SD_DARK, SD_LARGE, SD_REMEMBER, SD_RESULTS, SD_QUALITY, SD_CACHE,
    SD_PREFETCH, SD_THREADS, SD_VOLUME, SD_MUTED, SD_MODE, SD_BRIDGE,
    SD_SEARCH_TIMEOUT, SD_RESOLVE_TIMEOUT, SD_PRESET, SD_PRESET_APPLY
};

typedef struct {
    HWND window, controls[48], fields[SD_RESOLVE_TIMEOUT - SD_DARK + 1], feedback, preset_hint;
    int pages[48], count, page;
    BOOL small_font[48];
    AppSettings draft;
    UiStyle style;
    const WCHAR *base;
    WCHAR *notice;
    size_t notice_cap;
    BOOL accepted, failed;
} SettingsDialog;

static HWND setting_field(SettingsDialog *d, int id) {
    return d->fields[id - SD_DARK];
}

static HWND setting_control(SettingsDialog *d, const WCHAR *class_name,
                            const WCHAR *text, DWORD style, int id,
                            int page, int x, int y, int width, int height) {
    DWORD ex = !lstrcmpW(class_name, L"EDIT") ? WS_EX_CLIENTEDGE : 0;
    HWND control = CreateWindowExW(ex, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, d->window,
                                   (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (!control) { d->failed = TRUE; return NULL; }
    SendMessageW(control, WM_SETFONT, (WPARAM)d->style.normal, FALSE);
    if (d->count < (int)COUNT(d->controls)) {
        d->controls[d->count] = control;
        d->pages[d->count++] = page;
    }
    if (id >= SD_DARK && id <= SD_RESOLVE_TIMEOUT) d->fields[id - SD_DARK] = control;
    return control;
}

static void setting_label(SettingsDialog *d, const WCHAR *text, int page, int x, int y, int width) {
    setting_control(d, L"STATIC", text, SS_LEFT, 0, page, x, y, width, 24);
}

static void setting_hint(SettingsDialog *d, const WCHAR *text, int page, int y) {
    HWND control = setting_control(d, L"STATIC", text, SS_LEFT, 0, page, 28, y, 480, 24);
    if (control) {
        d->small_font[d->count - 1] = TRUE;
        SendMessageW(control, WM_SETFONT, (WPARAM)d->style.small, FALSE);
    }
}

static HWND setting_combo(SettingsDialog *d, int id, int page, int y,
                          const WCHAR *label, const WCHAR *a, const WCHAR *b, const WCHAR *c) {
    HWND combo;
    setting_label(d, label, page, 28, y + 4, 285);
    combo = setting_control(d, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                            id, page, 326, y, 182, 150);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)a);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)b);
    if (c) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)c);
    return combo;
}

static void setting_select(SettingsDialog *d, int id, int selection) {
    SendMessageW(setting_field(d, id), CB_SETCURSEL, (WPARAM)selection, 0);
}

static int setting_selection(SettingsDialog *d, int id) {
    return (int)SendMessageW(setting_field(d, id), CB_GETCURSEL, 0, 0);
}

static void setting_check(SettingsDialog *d, int id, BOOL checked) {
    SendMessageW(setting_field(d, id), BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}

static BOOL setting_checked(SettingsDialog *d, int id) {
    return SendMessageW(setting_field(d, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void setting_values(SettingsDialog *d) {
    WCHAR number[16];
    AppSettings *s = &d->draft;
    setting_check(d, SD_DARK, s->dark_theme);
    setting_check(d, SD_LARGE, s->large_text);
    setting_check(d, SD_REMEMBER, s->remember_size);
    setting_check(d, SD_MUTED, s->muted);
    setting_select(d, SD_RESULTS, s->results_limit == 6 ? 0 : 1);
    setting_select(d, SD_QUALITY, s->quality == 144 ? 0 : s->quality == 240 ? 1 : 2);
    wsprintfW(number, L"%d", s->cache_ms);
    SetWindowTextW(setting_field(d, SD_CACHE), number);
    setting_select(d, SD_PREFETCH, s->prefetch_kib == 64 ? 0 : s->prefetch_kib == 128 ? 1 : 2);
    setting_select(d, SD_THREADS, s->decoder_threads - 1);
    wsprintfW(number, L"%d", s->volume);
    SetWindowTextW(setting_field(d, SD_VOLUME), number);
    setting_select(d, SD_MODE, s->mode);
    SetWindowTextW(setting_field(d, SD_BRIDGE), s->bridge_url);
    setting_select(d, SD_SEARCH_TIMEOUT, s->search_timeout_s == 30 ? 0 : s->search_timeout_s == 60 ? 1 : 2);
    setting_select(d, SD_RESOLVE_TIMEOUT, s->resolve_timeout_s == 30 ? 0 : s->resolve_timeout_s == 60 ? 1 : 2);
}

static void setting_page(SettingsDialog *d, int page) {
    int i;
    d->page = page;
    for (i = 0; i < d->count; ++i) {
        ShowWindow(d->controls[i], d->pages[i] < 0 || d->pages[i] == page ? SW_SHOW : SW_HIDE);
    }
    InvalidateRect(d->window, NULL, TRUE);
}

static void setting_restyle(SettingsDialog *d) {
    int i;
    ui_style_free(&d->style);
    ui_style_init(&d->style, setting_checked(d, SD_DARK), setting_checked(d, SD_LARGE));
    for (i = 0; i < d->count; ++i) {
        SendMessageW(d->controls[i], WM_SETFONT,
                     (WPARAM)(d->small_font[i] ? d->style.small : d->style.normal), FALSE);
    }
    RedrawWindow(d->window, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

static BOOL setting_read(SettingsDialog *d, BOOL check_ranges) {
    WCHAR text[32];
    AppSettings *s = &d->draft;
    int selection, cache, volume;
    GetWindowTextW(setting_field(d, SD_CACHE), text, COUNT(text));
    cache = parse_number(text, -1);
    GetWindowTextW(setting_field(d, SD_VOLUME), text, COUNT(text));
    volume = parse_number(text, -1);
    if (check_ranges && (cache < 250 || cache > 3000)) {
        setting_page(d, 1);
        SetWindowTextW(d->feedback, L"El búfer debe estar entre 250 y 3000 ms.");
        SetFocus(setting_field(d, SD_CACHE));
        return FALSE;
    }
    if (check_ranges && (volume < 0 || volume > 100)) {
        setting_page(d, 1);
        SetWindowTextW(d->feedback, L"El volumen debe estar entre 0 y 100.");
        SetFocus(setting_field(d, SD_VOLUME));
        return FALSE;
    }
    s->dark_theme = setting_checked(d, SD_DARK);
    s->large_text = setting_checked(d, SD_LARGE);
    s->remember_size = setting_checked(d, SD_REMEMBER);
    s->muted = setting_checked(d, SD_MUTED);
    s->results_limit = setting_selection(d, SD_RESULTS) == 0 ? 6 : 12;
    selection = setting_selection(d, SD_QUALITY);
    s->quality = selection == 0 ? 144 : selection == 2 ? 360 : 240;
    s->cache_ms = cache;
    selection = setting_selection(d, SD_PREFETCH);
    s->prefetch_kib = selection == 0 ? 64 : selection == 2 ? 256 : 128;
    s->decoder_threads = setting_selection(d, SD_THREADS) == 1 ? 2 : 1;
    s->volume = volume;
    s->mode = setting_selection(d, SD_MODE) == 1 ? 1 : 0;
    GetWindowTextW(setting_field(d, SD_BRIDGE), s->bridge_url, COUNT(s->bridge_url));
    selection = setting_selection(d, SD_SEARCH_TIMEOUT);
    s->search_timeout_s = selection == 0 ? 30 : selection == 1 ? 60 : 120;
    selection = setting_selection(d, SD_RESOLVE_TIMEOUT);
    s->resolve_timeout_s = selection == 0 ? 30 : selection == 1 ? 60 : 90;
    settings_validate(s);
    return TRUE;
}

static void setting_preset_hint(SettingsDialog *d) {
    int selected = (int)SendMessageW(GetDlgItem(d->window, SD_PRESET), CB_GETCURSEL, 0, 0);
    BOOL valid = selected >= 0 && selected < PRESET_COUNT;
    SetWindowTextW(d->preset_hint, valid ? presets[selected].description : L"Elegí un preset para preparar.");
    EnableWindow(GetDlgItem(d->window, SD_PRESET_APPLY), valid);
}

static BOOL setting_create(SettingsDialog *d) {
    HWND control;
    int i;
    setting_control(d, L"BUTTON", L"General", WS_TABSTOP | BS_OWNERDRAW, SD_GENERAL, -1, 20, 61, 158, 32);
    setting_control(d, L"BUTTON", L"Reproducción", WS_TABSTOP | BS_OWNERDRAW, SD_PLAYBACK, -1, 188, 61, 158, 32);
    setting_control(d, L"BUTTON", L"Red", WS_TABSTOP | BS_OWNERDRAW, SD_NETWORK, -1, 356, 61, 158, 32);
    setting_control(d, L"BUTTON", L"Tema oscuro", WS_TABSTOP | BS_AUTOCHECKBOX, SD_DARK, 0, 28, 118, 470, 27);
    setting_control(d, L"BUTTON", L"Texto grande", WS_TABSTOP | BS_AUTOCHECKBOX, SD_LARGE, 0, 28, 156, 470, 27);
    setting_control(d, L"BUTTON", L"Recordar el tamaño de la ventana", WS_TABSTOP | BS_AUTOCHECKBOX, SD_REMEMBER, 0, 28, 194, 480, 27);
    setting_combo(d, SD_RESULTS, 0, 238, L"Resultados por búsqueda", L"6 resultados", L"12 resultados", NULL);
    setting_hint(d, L"Presets de reproducción", 0, 282);
    control = setting_control(d, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                              SD_PRESET, 0, 28, 310, 350, 180);
    for (i = 0; i < PRESET_COUNT; ++i)
        SendMessageW(control, CB_ADDSTRING, 0, (LPARAM)presets[i].name);
    SendMessageW(control, CB_SETCURSEL, PRESET_ATOM, 0);
    setting_control(d, L"BUTTON", L"Preparar", WS_TABSTOP | BS_OWNERDRAW,
                    SD_PRESET_APPLY, 0, 390, 310, 118, 32);
    d->preset_hint = setting_control(d, L"STATIC", L"", SS_LEFT, 0, 0, 28, 349, 480, 24);
    if (d->preset_hint) {
        d->small_font[d->count - 1] = TRUE;
        SendMessageW(d->preset_hint, WM_SETFONT, (WPARAM)d->style.small, FALSE);
    }
    setting_combo(d, SD_QUALITY, 1, 112, L"Calidad de video", L"144p", L"240p · Atom", L"360p");
    setting_label(d, L"Búfer de red (250 a 3000 ms)", 1, 28, 158, 290);
    control = setting_control(d, L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, SD_CACHE, 1, 326, 154, 182, 27);
    SendMessageW(control, EM_SETLIMITTEXT, 4, 0);
    setting_combo(d, SD_PREFETCH, 1, 196, L"Lectura anticipada", L"64 KiB", L"128 KiB · Atom", L"256 KiB");
    setting_combo(d, SD_THREADS, 1, 238, L"Hilos del decodificador", L"1 hilo · Atom", L"2 hilos", NULL);
    setting_label(d, L"Volumen (0 a 100)", 1, 28, 284, 290);
    control = setting_control(d, L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, SD_VOLUME, 1, 326, 280, 182, 27);
    SendMessageW(control, EM_SETLIMITTEXT, 3, 0);
    setting_control(d, L"BUTTON", L"Silenciar el audio", WS_TABSTOP | BS_AUTOCHECKBOX, SD_MUTED, 1, 28, 325, 480, 27);
    setting_combo(d, SD_MODE, 2, 112, L"Modo de conexión", L"Directo", L"Puente", NULL);
    setting_label(d, L"Dirección del puente", 2, 28, 158, 480);
    control = setting_control(d, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, SD_BRIDGE, 2, 28, 188, 480, 28);
    SendMessageW(control, EM_SETLIMITTEXT, 255, 0);
    setting_hint(d, L"El token después de ? se usa sólo durante esta sesión.", 2, 224);
    setting_combo(d, SD_SEARCH_TIMEOUT, 2, 268, L"Espera máxima de búsqueda", L"30 segundos", L"60 segundos", L"120 segundos");
    setting_combo(d, SD_RESOLVE_TIMEOUT, 2, 314, L"Espera máxima de video", L"30 segundos", L"60 segundos", L"90 segundos");
    d->feedback = setting_control(d, L"STATIC", L"Los cambios se aplican al guardar.", SS_LEFT, 0, -1, 20, 373, 500, 25);
    if (d->feedback) {
        d->small_font[d->count - 1] = TRUE;
        SendMessageW(d->feedback, WM_SETFONT, (WPARAM)d->style.small, FALSE);
    }
    setting_control(d, L"BUTTON", L"Restablecer", WS_TABSTOP | BS_OWNERDRAW, SD_DEFAULTS, -1, 20, 410, 146, 32);
    setting_control(d, L"BUTTON", L"Cancelar", WS_TABSTOP | BS_OWNERDRAW, IDCANCEL, -1, 277, 410, 112, 32);
    setting_control(d, L"BUTTON", L"Guardar", WS_TABSTOP | BS_OWNERDRAW, IDOK, -1, 400, 410, 114, 32);
    if (d->failed) return FALSE;
    setting_values(d);
    setting_preset_hint(d);
    setting_page(d, 0);
    return TRUE;
}

static LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    SettingsDialog *d = (SettingsDialog *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        d = (SettingsDialog *)((CREATESTRUCTW *)lp)->lpCreateParams;
        d->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)d);
    }
    if (!d) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_CREATE:
        return setting_create(d) ? 0 : -1;
    case DM_GETDEFID:
        return MAKELRESULT(IDOK, DC_HASDEFID);
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case SD_GENERAL: setting_page(d, 0); return 0;
        case SD_PLAYBACK: setting_page(d, 1); return 0;
        case SD_NETWORK: setting_page(d, 2); return 0;
        case SD_PRESET:
            if (HIWORD(wp) == CBN_SELCHANGE) setting_preset_hint(d);
            return 0;
        case SD_PRESET_APPLY: {
            WCHAR volume_text[32];
            int volume;
            int selected = (int)SendMessageW(GetDlgItem(window, SD_PRESET), CB_GETCURSEL, 0, 0);
            if (selected < 0 || selected >= PRESET_COUNT) return 0;
            /* The preset replaces cache settings but keeps volume. Do not
             * silently turn an invalid volume edit into its default value. */
            GetWindowTextW(setting_field(d, SD_VOLUME), volume_text, COUNT(volume_text));
            volume = parse_number(volume_text, -1);
            if (volume < 0 || volume > 100) {
                setting_page(d, 1);
                SetWindowTextW(d->feedback, L"El volumen debe estar entre 0 y 100.");
                SetFocus(setting_field(d, SD_VOLUME));
                return 0;
            }
            /* Preserve edits on every page before changing the five preset
             * fields. Preparing never saves; Cancel still discards the draft. */
            setting_read(d, FALSE);
            if (settings_apply_preset(&d->draft, selected)) {
                setting_values(d);
                SetWindowTextW(d->feedback, L"Preset preparado; pulsá Guardar para aplicar.");
            }
            return 0;
        }
        case SD_DARK:
        case SD_LARGE:
            if (HIWORD(wp) == BN_CLICKED) setting_restyle(d);
            return 0;
        case SD_DEFAULTS: {
            int width = d->draft.width, height = d->draft.height;
            settings_defaults(&d->draft);
            d->draft.width = width;
            d->draft.height = height;
            setting_values(d);
            SendMessageW(GetDlgItem(window, SD_PRESET), CB_SETCURSEL, PRESET_ATOM, 0);
            setting_preset_hint(d);
            setting_restyle(d);
            SetWindowTextW(d->feedback, L"Valores predeterminados; pulsá Guardar para aplicar.");
            return 0;
        }
        case SD_ATOM:
        case SD_MINIMUM:
            setting_read(d, FALSE);
            d->draft.quality = LOWORD(wp) == SD_ATOM ? 240 : 144;
            d->draft.cache_ms = LOWORD(wp) == SD_ATOM ? 450 : 250;
            d->draft.prefetch_kib = LOWORD(wp) == SD_ATOM ? 128 : 64;
            d->draft.decoder_threads = 1;
            setting_values(d);
            SendMessageW(GetDlgItem(window, SD_PRESET), CB_SETCURSEL,
                         LOWORD(wp) == SD_ATOM ? PRESET_ATOM : PRESET_MINIMUM, 0);
            setting_preset_hint(d);
            SetWindowTextW(d->feedback, L"Perfil preparado; pulsá Guardar para aplicar.");
            return 0;
        case IDOK:
            if (!setting_read(d, TRUE)) return 0;
            settings_save(&d->draft, d->base, d->notice, d->notice_cap);
            d->accepted = TRUE;
            DestroyWindow(window);
            return 0;
        case IDCANCEL:
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT r, active;
        HFONT old;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &r);
        FillRect(dc, &r, d->style.background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, d->style.fg);
        old = (HFONT)SelectObject(dc, d->style.title);
        r.left = 20; r.top = 14; r.right = 514; r.bottom = 49;
        DrawTextW(dc, L"Configuración", -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, old);
        active.left = 20 + d->page * 168; active.right = active.left + 158;
        active.top = 96; active.bottom = 99;
        {
            HBRUSH accent = CreateSolidBrush(d->style.accent);
            if (accent) { FillRect(dc, &active, accent); DeleteObject(accent); }
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)ui_control_color(&d->style, message, (HDC)wp, (HWND)lp);
    case WM_DRAWITEM:
        if (ui_draw_button(&d->style, (const DRAWITEMSTRUCT *)lp)) return TRUE;
        break;
    }
    return DefWindowProcW(window, message, wp, lp);
}

BOOL settings_dialog(HWND owner, AppSettings *settings, const WCHAR *base,
                     WCHAR *notice, size_t cap) {
    static const WCHAR class_name[] = L"TubeLiviano.Settings";
    WNDCLASSW cls;
    SettingsDialog dialog;
    RECT r = {0, 0, 540, 454}, work, parent;
    HWND window;
    MSG message;
    BOOL disabled_owner, quit = FALSE;
    int x, y, result;
    if (!settings) return FALSE;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.draft = *settings;
    settings_validate(&dialog.draft);
    dialog.base = base;
    dialog.notice = notice;
    dialog.notice_cap = cap;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = settings_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.lpszClassName = class_name;
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        set_notice(notice, cap, L"No se pudo abrir Configuración.");
        return FALSE;
    }
    ui_style_init(&dialog.style, dialog.draft.dark_theme, dialog.draft.large_text);
    AdjustWindowRectEx(&r, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, FALSE, WS_EX_CONTROLPARENT);
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    parent = work;
    if (IsWindow(owner)) GetWindowRect(owner, &parent);
    x = parent.left + ((parent.right - parent.left) - (r.right - r.left)) / 2;
    y = parent.top + ((parent.bottom - parent.top) - (r.bottom - r.top)) / 2;
    if (x + r.right - r.left > work.right) x = work.right - (r.right - r.left);
    if (y + r.bottom - r.top > work.bottom) y = work.bottom - (r.bottom - r.top);
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    window = CreateWindowExW(WS_EX_CONTROLPARENT, class_name, L"Configuración · TubeLiviano",
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                             x, y, r.right - r.left, r.bottom - r.top, owner, NULL,
                             cls.hInstance, &dialog);
    if (!window) {
        ui_style_free(&dialog.style);
        set_notice(notice, cap, L"No se pudo abrir Configuración.");
        return FALSE;
    }
    disabled_owner = IsWindow(owner) && IsWindowEnabled(owner);
    if (disabled_owner) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    SetFocus(GetDlgItem(window, SD_GENERAL));
    while (IsWindow(window)) {
        result = GetMessageW(&message, NULL, 0, 0);
        if (result <= 0) {
            quit = result == 0;
            dialog.accepted = FALSE;
            DestroyWindow(window);
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (disabled_owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    ui_style_free(&dialog.style);
    if (dialog.accepted) *settings = dialog.draft;
    if (quit) PostQuitMessage((int)message.wParam);
    return dialog.accepted;
}
