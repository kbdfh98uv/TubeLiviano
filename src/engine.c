#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define OUTPUT_LIMIT (1024 * 1024)
#define STDERR_LIMIT 8192
#define COMMAND_CAP 32768

static void message(WCHAR *out, size_t cap, const WCHAR *text)
{
    if (!out || !cap) return;
    wcsncpy(out, text, cap - 1);
    out[cap - 1] = 0;
}

BOOL engine_init(Engine *engine, const WCHAR *base, WCHAR *error, size_t cap)
{
    typedef LONG (WINAPI *RtlVersionFn)(OSVERSIONINFOW *);
    RtlVersionFn version_fn;
    OSVERSIONINFOW version;
    size_t n;
    if (!engine || !base || !(n = wcslen(base)) || n > MAX_PATH - 40) {
        message(error, cap, L"Ruta portable demasiado larga (use una carpeta corta).");
        return FALSE;
    }
    ZeroMemory(engine, sizeof(*engine));
    ZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    version_fn = (RtlVersionFn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (!version_fn || version_fn(&version) != 0) {
        message(error, cap, L"No se pudo detectar la version real de Windows; no se cargara un motor al azar.");
        return FALSE;
    }
    engine->xp = version.dwMajorVersion < 10;
    wcscpy(engine->base, base);
    while (n > 3 && (engine->base[n - 1] == L'\\' || engine->base[n - 1] == L'/')) engine->base[--n] = 0;
    wcscpy(engine->exe, engine->base);
    wcscat(engine->exe, engine->xp ? L"\\engines\\xp\\yt-dlp.exe" : L"\\engines\\modern\\yt-dlp.exe");
    if (GetFileAttributesW(engine->exe) == INVALID_FILE_ATTRIBUTES) {
        message(error, cap, engine->xp
                ? L"Falta engines\\xp\\yt-dlp.exe. Extraiga el ZIP completo; Windows antiguo usa el motor XP."
                : L"Falta engines\\modern\\yt-dlp.exe. Extraiga el ZIP completo; Windows 10/11 usa el motor oficial x86.");
        return FALSE;
    }
    message(error, cap, L"");
    return TRUE;
}

/* Windows CommandLineToArgvW / MS CRT quoting. Do not invoke a shell. */
static BOOL append_arg(WCHAR *command, size_t *used, const WCHAR *arg)
{
    size_t slashes;
    const WCHAR *p = arg;
#define PUT(c) do { if (*used >= COMMAND_CAP - 1) return FALSE; command[(*used)++] = (c); } while (0)
    if (*used) PUT(L' ');
    PUT(L'"');
    while (*p) {
        slashes = 0;
        while (*p == L'\\') { ++slashes; ++p; }
        if (!*p) {
            while (slashes--) { PUT(L'\\'); PUT(L'\\'); }
            break;
        }
        if (*p == L'"') {
            while (slashes--) { PUT(L'\\'); PUT(L'\\'); }
            PUT(L'\\'); PUT(*p++);
        } else {
            while (slashes--) PUT(L'\\');
            PUT(*p++);
        }
    }
    PUT(L'"');
    command[*used] = 0;
#undef PUT
    return TRUE;
}

static BOOL drain(HANDLE pipe, char *out, DWORD limit, DWORD *used, BOOL truncate)
{
    DWORD available, got, want;
    char discard[2048];
    /* Limit each drain so cancellation remains responsive under a noisy child. */
    DWORD budget = 65536;
    while (budget && PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL) && available) {
        want = available < budget ? available : budget;
        if (*used == limit) {
            if (!truncate) return FALSE;
            if (want > sizeof(discard)) want = sizeof(discard);
            if (!ReadFile(pipe, discard, want, &got, NULL) || !got) break;
        } else {
            if (want > limit - *used) want = limit - *used;
            if (!ReadFile(pipe, out + *used, want, &got, NULL) || !got) break;
            *used += got;
            out[*used] = 0;
        }
        budget -= got;
    }
    return TRUE;
}

static BOOL run(Engine *engine, const WCHAR *const *args, size_t argc,
                HANDLE cancel, DWORD timeout, char **output, WCHAR *error, size_t cap)
{
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    HANDLE readout = NULL, writeout = NULL, readerr = NULL, writeerr = NULL;
    HANDLE input = INVALID_HANDLE_VALUE, job = NULL;
    WCHAR *command = NULL;
    char *out = NULL, *err = NULL;
    DWORD outlen = 0, errlen = 0, start, exitcode = 1;
    size_t i, used = 0;
    BOOL ok = FALSE, started = FALSE;
    *output = NULL;
    ZeroMemory(&process, sizeof(process));
    command = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, COMMAND_CAP * sizeof(WCHAR));
    out = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, OUTPUT_LIMIT + 1);
    err = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, STDERR_LIMIT + 1);
    if (!command || !out || !err) {
        message(error, cap, L"Memoria insuficiente para iniciar el motor.");
        goto cleanup;
    }
    if (!append_arg(command, &used, engine->exe)) goto too_long;
    for (i = 0; i < argc; ++i) if (!append_arg(command, &used, args[i])) goto too_long;
    if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) {
        message(error, cap, L"Cancelado."); goto cleanup;
    }
    ZeroMemory(&sa, sizeof(sa)); sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    if (!CreatePipe(&readout, &writeout, &sa, 0) || !CreatePipe(&readerr, &writeerr, &sa, 0) ||
        !SetHandleInformation(readout, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(readerr, HANDLE_FLAG_INHERIT, 0)) {
        message(error, cap, L"No se pudieron crear los canales del motor."); goto cleanup;
    }
    input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    job = CreateJobObjectW(NULL, NULL);
    ZeroMemory(&limits, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        /* XP supports explicit TerminateJobObject but not kill-on-close.
         * We always terminate explicitly before closing our job handle. */
        if (GetLastError() == ERROR_INVALID_PARAMETER) limits.BasicLimitInformation.LimitFlags = 0;
        else { CloseHandle(job); job = NULL; }
        if (job && !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job); job = NULL;
        }
    }
    if (input == INVALID_HANDLE_VALUE || !job) {
        message(error, cap, L"No se pudo preparar la cancelacion segura del motor."); goto cleanup;
    }
    ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = input; startup.hStdOutput = writeout; startup.hStdError = writeerr;
    if (!CreateProcessW(engine->exe, command, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, engine->base, &startup, &process)) {
        if (GetLastError() == ERROR_BAD_EXE_FORMAT)
            message(error, cap, L"Motor incompatible: compruebe que ambos motores sean x86 y el motor XP tenga su framework.");
        else
            message(error, cap, L"No se pudo iniciar yt-dlp. Extraiga el ZIP completo y revise antivirus/permisos.");
        goto cleanup;
    }
    started = TRUE;
    /* Assign before ResumeThread, so even immediately-spawned descendants belong to us. */
    if (!AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == (DWORD)-1) {
        message(error, cap, L"Windows no permite controlar el proceso del motor. No se iniciara sin cancelacion segura.");
        goto cleanup;
    }
    CloseHandle(writeout); writeout = NULL;
    CloseHandle(writeerr); writeerr = NULL;
    start = GetTickCount();
    for (;;) {
        if (cancel && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) {
            message(error, cap, L"Cancelado."); goto cleanup;
        }
        if ((DWORD)(GetTickCount() - start) >= timeout) {
            message(error, cap, L"El motor excedio el tiempo de espera. Reintente o cambie de calidad; tambien puede usar el puente.");
            goto cleanup;
        }
        if (!drain(readout, out, OUTPUT_LIMIT, &outlen, FALSE)) {
            message(error, cap, L"La respuesta del motor excede el limite de memoria."); goto cleanup;
        }
        drain(readerr, err, STDERR_LIMIT, &errlen, TRUE);
        if (WaitForSingleObject(process.hProcess, 15) == WAIT_OBJECT_0) break;
    }
    /* No descendant may outlive its extractor, even after a successful extraction. */
    TerminateJobObject(job, 0);
    if (!drain(readout, out, OUTPUT_LIMIT, &outlen, FALSE)) {
        message(error, cap, L"La respuesta del motor excede el limite de memoria."); goto cleanup;
    }
    drain(readerr, err, STDERR_LIMIT, &errlen, TRUE);
    GetExitCodeProcess(process.hProcess, &exitcode);
    if (exitcode != 0) {
        WCHAR detail[1024];
        int chars = MultiByteToWideChar(CP_UTF8, 0, err, (int)(errlen > 900 ? 900 : errlen), detail, 1023);
        detail[chars > 0 ? chars : 0] = 0;
        if (chars > 0) message(error, cap, detail);
        else message(error, cap, L"yt-dlp fallo sin detalles. Puede faltar un framework o YouTube pedir verificacion/HTTP 429.");
        goto cleanup;
    }
    *output = out; out = NULL; ok = TRUE;
    message(error, cap, L"");
    goto cleanup;
too_long:
    message(error, cap, L"La consulta o enlace es demasiado largo.");
cleanup:
    if (started && !ok) { TerminateJobObject(job, 1); TerminateProcess(process.hProcess, 1); WaitForSingleObject(process.hProcess, 1000); }
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    if (job) CloseHandle(job);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (readout) CloseHandle(readout);
    if (writeout) CloseHandle(writeout);
    if (readerr) CloseHandle(readerr);
    if (writeerr) CloseHandle(writeerr);
    if (command) HeapFree(GetProcessHeap(), 0, command);
    if (err) HeapFree(GetProcessHeap(), 0, err);
    if (out) HeapFree(GetProcessHeap(), 0, out);
    return ok;
}

/* Bounded JSON reader. Search output is one top-level object per line.
 * Nested metadata is skipped, never mistaken for a result field. */
typedef struct { const char *p, *end; } Json;
static void space(Json *j) { while (j->p < j->end && (unsigned char)*j->p <= 32) ++j->p; }
static int hex4(Json *j)
{
    int n = 0, i, c;
    for (i = 0; i < 4; ++i) {
        if (j->p == j->end) return -1;
        c = (unsigned char)*j->p++;
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else return -1;
        n = (n << 4) | c;
    }
    return n;
}
static void byte_out(char *out, size_t cap, size_t *n, unsigned char c)
{
    if (out && *n + 1 < cap) out[*n] = (char)c;
    ++*n;
}
static BOOL json_string(Json *j, char *out, size_t cap)
{
    size_t n = 0;
    unsigned char c;
    int u, low;
    if (j->p == j->end || *j->p++ != '"') return FALSE;
    while (j->p < j->end) {
        c = (unsigned char)*j->p++;
        if (c == '"') {
            if (out && cap) out[n < cap ? n : cap - 1] = 0;
            return TRUE;
        }
        if (c < 32) return FALSE;
        if (c != '\\') { byte_out(out, cap, &n, c); continue; }
        if (j->p == j->end) return FALSE;
        c = (unsigned char)*j->p++;
        switch (c) {
        case '"': case '\\': case '/': byte_out(out, cap, &n, c); break;
        case 'b': byte_out(out, cap, &n, '\b'); break;
        case 'f': byte_out(out, cap, &n, '\f'); break;
        case 'n': case 'r': case 't': byte_out(out, cap, &n, ' '); break;
        case 'u':
            u = hex4(j); if (u < 0) return FALSE;
            if (u >= 0xd800 && u <= 0xdbff) {
                if (j->end - j->p < 6 || j->p[0] != '\\' || j->p[1] != 'u') return FALSE;
                j->p += 2; low = hex4(j);
                if (low < 0xdc00 || low > 0xdfff) return FALSE;
                u = 0x10000 + ((u - 0xd800) << 10) + low - 0xdc00;
            } else if (u >= 0xdc00 && u <= 0xdfff) return FALSE;
            if (!u) return FALSE; /* Never let escaped NUL hide an identifier suffix. */
            if (u < 0x80) byte_out(out, cap, &n, (unsigned char)u);
            else if (u < 0x800) {
                byte_out(out, cap, &n, (unsigned char)(0xc0 | (u >> 6)));
                byte_out(out, cap, &n, (unsigned char)(0x80 | (u & 63)));
            } else if (u < 0x10000) {
                byte_out(out, cap, &n, (unsigned char)(0xe0 | (u >> 12)));
                byte_out(out, cap, &n, (unsigned char)(0x80 | ((u >> 6) & 63)));
                byte_out(out, cap, &n, (unsigned char)(0x80 | (u & 63)));
            } else {
                byte_out(out, cap, &n, (unsigned char)(0xf0 | (u >> 18)));
                byte_out(out, cap, &n, (unsigned char)(0x80 | ((u >> 12) & 63)));
                byte_out(out, cap, &n, (unsigned char)(0x80 | ((u >> 6) & 63)));
                byte_out(out, cap, &n, (unsigned char)(0x80 | (u & 63)));
            }
            break;
        default: return FALSE;
        }
    }
    return FALSE;
}

static BOOL skip_value(Json *j, int depth)
{
    char closing;
    const char *start, *p;
    if (depth > 32) return FALSE;
    space(j); if (j->p == j->end) return FALSE;
    if (*j->p == '"') return json_string(j, NULL, 0);
    if (*j->p == '{' || *j->p == '[') {
        closing = *j->p++ == '{' ? '}' : ']';
        space(j);
        if (j->p < j->end && *j->p == closing) { ++j->p; return TRUE; }
        for (;;) {
            if (closing == '}') {
                if (!json_string(j, NULL, 0)) return FALSE;
                space(j); if (j->p == j->end || *j->p++ != ':') return FALSE;
            }
            if (!skip_value(j, depth + 1)) return FALSE;
            space(j); if (j->p == j->end) return FALSE;
            if (*j->p == closing) { ++j->p; return TRUE; }
            if (*j->p++ != ',') return FALSE;
            space(j);
        }
    }
    start = j->p;
    while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']' && (unsigned char)*j->p > 32) ++j->p;
    if ((j->p - start == 4 && (!memcmp(start, "null", 4) || !memcmp(start, "true", 4))) ||
        (j->p - start == 5 && !memcmp(start, "false", 5))) return TRUE;
    p = start;
    if (p < j->p && *p == '-') ++p;
    if (p == j->p) return FALSE;
    if (*p == '0') ++p;
    else {
        if (*p < '1' || *p > '9') return FALSE;
        while (p < j->p && *p >= '0' && *p <= '9') ++p;
    }
    if (p < j->p && *p == '.') {
        ++p;
        if (p == j->p || *p < '0' || *p > '9') return FALSE;
        while (p < j->p && *p >= '0' && *p <= '9') ++p;
    }
    if (p < j->p && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < j->p && (*p == '+' || *p == '-')) ++p;
        if (p == j->p || *p < '0' || *p > '9') return FALSE;
        while (p < j->p && *p >= '0' && *p <= '9') ++p;
    }
    return p == j->p;
}

/* Integral seconds, rounded down, without floating point/locale/strtod code.
 * JSON has already validated the number. Preserve scientific notation too. */
static int duration_seconds(const char *begin, const char *end)
{
    const char *p = begin, *first = NULL, *number_end;
    int digit_index = 0, first_index = 0, integer_digits = 0, exponent = 0, sign = 1;
    int before_point = 1, wanted, n;
    unsigned int value = 0;
    while (p < end && *p != 'e' && *p != 'E') {
        if (*p == '.') before_point = 0;
        else {
            if (!first && *p != '0') { first = p; first_index = digit_index; }
            if (before_point) ++integer_digits;
            ++digit_index;
        }
        ++p;
    }
    number_end = p;
    if (p < end) {
        ++p;
        if (p < end && (*p == '-' || *p == '+')) { if (*p == '-') sign = -1; ++p; }
        while (p < end) {
            if (exponent < 10000) exponent = exponent * 10 + *p - '0';
            ++p;
        }
    }
    if (!first) return 0;
    wanted = integer_digits - first_index + sign * exponent;
    if (wanted <= 0) return 0;
    if (wanted > 10) return -1;
    p = first;
    for (n = 0; n < wanted; ++n) {
        unsigned int digit;
        if (p < number_end && *p == '.') ++p;
        digit = p < number_end ? (unsigned int)(*p++ - '0') : 0;
        if (value > (2147483647u - digit) / 10u) return -1;
        value = value * 10u + digit;
    }
    return (int)value;
}

static void utf8_wide(const char *s, WCHAR *out, int cap)
{
    WCHAR temporary[2048];
    int n = (int)strlen(s), got, take;
    /* Decode before truncating so neither UTF-8 nor UTF-16 is split. */
    got = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, temporary, 2047);
    while (!got && n > 0 && (int)strlen(s) - n < 3) {
        --n;
        got = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, temporary, 2047);
    }
    take = got < cap - 1 ? got : cap - 1;
    if (take && temporary[take - 1] >= 0xd800 && temporary[take - 1] <= 0xdbff) --take;
    if (take) memcpy(out, temporary, (size_t)take * sizeof(WCHAR));
    out[take] = 0;
}

static BOOL parse_result(const char *line, size_t len, EngineResult *result)
{
    Json j;
    char key[64], value[2048], uploader[1024] = "";
    int i;
    j.p = line; j.end = line + len;
    ZeroMemory(result, sizeof(*result)); result->duration = -1;
    space(&j); if (j.p == j.end || *j.p++ != '{') return FALSE;
    space(&j);
    if (j.p < j.end && *j.p == '}') return FALSE;
    for (;;) {
        if (!json_string(&j, key, sizeof(key))) return FALSE;
        space(&j); if (j.p == j.end || *j.p++ != ':') return FALSE;
        space(&j);
        if (j.p == j.end) return FALSE;
        if (*j.p == '"' && (!strcmp(key, "title") || !strcmp(key, "channel") ||
                              !strcmp(key, "uploader") || !strcmp(key, "id"))) {
            if (!json_string(&j, value, sizeof(value))) return FALSE;
            if (!strcmp(key, "title")) utf8_wide(value, result->title, 512);
            else if (!strcmp(key, "channel")) utf8_wide(value, result->channel, 256);
            else if (!strcmp(key, "uploader")) { strncpy(uploader, value, sizeof(uploader) - 1); uploader[sizeof(uploader) - 1] = 0; }
            else {
                if (strlen(value) != 11) return FALSE;
                for (i = 0; i < 11; ++i) {
                    char c = value[i];
                    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_')) return FALSE;
                    result->id[i] = c;
                }
            }
        } else if (!strcmp(key, "duration") && *j.p >= '0' && *j.p <= '9') {
            const char *number = j.p;
            if (!skip_value(&j, 0)) return FALSE;
            result->duration = duration_seconds(number, j.p);
        } else if (!skip_value(&j, 0)) return FALSE;
        space(&j); if (j.p == j.end) return FALSE;
        if (*j.p == '}') { ++j.p; break; }
        if (*j.p++ != ',') return FALSE;
        space(&j);
    }
    space(&j); if (j.p != j.end || !result->id[0]) return FALSE;
    if (!result->title[0]) wcscpy(result->title, result->id);
    if (!result->channel[0]) utf8_wide(uploader, result->channel, 256);
    return TRUE;
}

BOOL engine_search(Engine *engine, const WCHAR *query, HANDLE cancel,
                   EngineResult results[ENGINE_MAX_RESULTS], int *count,
                   WCHAR *error, size_t cap)
{
    WCHAR *search;
    char *output = NULL, *line, *end;
    size_t n;
    const WCHAR *args[] = {L"--ignore-config", L"--no-cache-dir", L"--no-warnings",
        L"--encoding", L"utf-8", L"--socket-timeout", L"12", L"--retries", L"0",
        L"--flat-playlist", L"--skip-download", L"--dump-json", L"--", NULL};
    *count = 0;
    n = wcslen(query);
    if (!n || n > 2048) { message(error, cap, L"Escriba una busqueda de hasta 2048 caracteres."); return FALSE; }
    search = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (n + 12) * sizeof(WCHAR));
    if (!search) { message(error, cap, L"Memoria insuficiente."); return FALSE; }
    wcscpy(search, L"ytsearch12:"); wcscat(search, query);
    args[sizeof(args) / sizeof(args[0]) - 1] = search;
    if (!run(engine, args, sizeof(args) / sizeof(args[0]), cancel, 120000, &output, error, cap)) {
        HeapFree(GetProcessHeap(), 0, search); return FALSE;
    }
    HeapFree(GetProcessHeap(), 0, search);
    line = output;
    while (*line && *count < ENGINE_MAX_RESULTS) {
        end = strchr(line, '\n'); if (!end) end = line + strlen(line);
        if (end != line && !parse_result(line, (size_t)(end - line), &results[*count])) {
            message(error, cap, L"El motor devolvio resultados invalidos. Reintente la busqueda.");
            HeapFree(GetProcessHeap(), 0, output); *count = 0; return FALSE;
        }
        if (end != line) ++*count;
        line = *end ? end + 1 : end;
    }
    HeapFree(GetProcessHeap(), 0, output);
    if (!*count) { message(error, cap, L"Sin resultados. Reintente con otro titulo o pegue un enlace."); return FALSE; }
    return TRUE;
}

BOOL engine_resolve(Engine *engine, const WCHAR *url, int height, BOOL fallback,
                    HANDLE cancel, EngineStreams *streams,
                    WCHAR *error, size_t cap)
{
    WCHAR selector[512];
    WCHAR jsruntime[MAX_PATH + 32];
    char *output = NULL, *line, *end;
    size_t n, k;
    int count = 0;
    const WCHAR *args[32];
    size_t argc = 0;
    ZeroMemory(streams, sizeof(*streams));
    if (height != 144 && height != 240 && height != 360) {
        message(error, cap, L"Perfil no valido: elija 144p, 240p o 360p."); return FALSE;
    }
    if (!url || wcslen(url) > 8192 ||
        (wcsncmp(url, L"https://", 8) && wcsncmp(url, L"http://", 7))) {
        message(error, cap, L"Use un enlace HTTP o HTTPS valido."); return FALSE;
    }
    /* Prefer a muxed AVC/AAC MP4, otherwise open two URLs directly in libVLC.
     * No merge, FFmpeg, full download, or high-quality fallback is required. */
    _snwprintf(selector, 512,
        L"best[height<=%d][fps<=30][vcodec^=avc1][acodec^=mp4a]/bestvideo[height<=%d][fps<=30][vcodec^=avc1]+bestaudio[acodec^=mp4a]",
        height, height);
    wcscpy(jsruntime, L"quickjs:");
    wcscat(jsruntime, engine->base);
    wcscat(jsruntime, L"\\runtimes\\qjs.exe");
#define ARG(a) args[argc++] = (a)
    ARG(L"--ignore-config"); ARG(L"--no-cache-dir"); ARG(L"--no-warnings");
    ARG(L"--no-playlist"); ARG(L"--encoding"); ARG(L"utf-8");
    ARG(L"--socket-timeout"); ARG(L"12"); ARG(L"--retries"); ARG(L"0");
    ARG(L"--extractor-retries"); ARG(L"0");
    ARG(L"--js-runtimes"); ARG(jsruntime);
    if (fallback) ARG(L"--check-formats");
    ARG(L"--get-url"); ARG(L"--format"); ARG(selector); ARG(L"--"); ARG(url);
#undef ARG
    if (!run(engine, args, argc, cancel, 90000, &output, error, cap)) return FALSE;
    line = output;
    while (*line) {
        end = strchr(line, '\n'); if (!end) end = line + strlen(line);
        n = (size_t)(end - line);
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) --n;
        if (n) {
            if (count >= 2 || n >= ENGINE_URL_CAP ||
                (strncmp(line, "https://", 8) && strncmp(line, "http://", 7))) {
                message(error, cap, L"El motor no devolvio uno o dos streams HTTP validos.");
                HeapFree(GetProcessHeap(), 0, output); ZeroMemory(streams, sizeof(*streams)); return FALSE;
            }
            for (k = 0; k < n; ++k) if ((unsigned char)line[k] <= 32 || line[k] == 127) {
                message(error, cap, L"El motor devolvio una URL con caracteres de control invalidos.");
                HeapFree(GetProcessHeap(), 0, output); ZeroMemory(streams, sizeof(*streams)); return FALSE;
            }
            memcpy(count ? streams->audio : streams->video, line, n);
            (count ? streams->audio : streams->video)[n] = 0;
            ++count;
        }
        line = *end ? end + 1 : end;
    }
    HeapFree(GetProcessHeap(), 0, output);
    if (!count) {
        message(error, cap, L"No hay un stream H.264/AAC dentro de este perfil. Cambie de calidad o use el puente."); return FALSE;
    }
    return TRUE;
}
