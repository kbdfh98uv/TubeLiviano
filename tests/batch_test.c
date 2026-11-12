/* Synthetic command-script regression fixture; does not decode any media.
 * Build: x86_64-w64-mingw32-gcc -municode -std=c99 -Os -Wall -Wextra -Werror
 *        tests/batch_test.c -static-libgcc -o build/batch_test.exe
 * Run: batch_test.exe <absolute scripts/probar-offline.cmd>
 * The same executable acts as a fake TubeLiviano.exe in an isolated TEMP.
 * It writes an approved report, then deliberately exits with the selected
 * DWORD. The driver observes that DWORD and tests the real cmd.exe script. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static int checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)

static BOOL path(WCHAR *out, const WCHAR *base, const WCHAR *name) {
    int n = _snwprintf(out, MAX_PATH, L"%ls\\%ls", base, name);
    return n >= 0 && n < MAX_PATH;
}

static BOOL write_bytes(const WCHAR *name, const char *bytes) {
    HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0, size = (DWORD)strlen(bytes);
    BOOL ok;
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(file, bytes, size, &written, NULL) && written == size;
    if (!CloseHandle(file)) ok = FALSE;
    return ok;
}

static BOOL read_text(const WCHAR *name, char *bytes, DWORD cap) {
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD count = 0;
    BOOL ok;
    if (file == INVALID_HANDLE_VALUE || cap < 2) return FALSE;
    ok = ReadFile(file, bytes, cap - 1, &count, NULL);
    bytes[count] = 0;
    if (!CloseHandle(file)) ok = FALSE;
    return ok && count < cap - 1;
}

static int occurrences(const char *text, const char *needle) {
    int count = 0;
    size_t length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL) { ++count; text += length; }
    return count;
}

static void cleanup(const WCHAR *base) {
    WCHAR pattern[MAX_PATH], name[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!path(pattern, base, L"*")) return;
    find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!lstrcmpW(data.cFileName,L".") || !lstrcmpW(data.cFileName,L"..")) continue;
            if (!path(name, base, data.cFileName)) continue;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) cleanup(name);
            else { SetFileAttributesW(name, FILE_ATTRIBUTE_NORMAL); DeleteFileW(name); }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(base);
}

static BOOL temporary_directory(WCHAR *out) {
    WCHAR root[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, root);
    if (!n || n >= MAX_PATH || !GetTempFileNameW(root, L"tlb", 0, out)) return FALSE;
    return DeleteFileW(out) && CreateDirectoryW(out, NULL);
}

static BOOL run(const WCHAR *application, WCHAR *command, const WCHAR *directory,
                DWORD *exit_code) {
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD waited;
    BOOL ok;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    if (!CreateProcessW(application, command, NULL, NULL, FALSE, 0, NULL,
                        directory, &startup, &process)) return FALSE;
    waited = WaitForSingleObject(process.hProcess, 30000);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 250);
        WaitForSingleObject(process.hProcess, 3000);
    }
    ok = waited == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ok;
}

static int fake_child(int argc, WCHAR **argv) {
    WCHAR text[32], *end;
    const WCHAR *report = NULL;
    unsigned long value;
    int i;
    for (i = 2; i + 1 < argc; ++i) if (!lstrcmpW(argv[i], L"--report")) report = argv[i + 1];
    if (!report || !GetEnvironmentVariableW(L"TL_BATCH_FIXTURE_EXIT", text, COUNT(text))) return 240;
    value = wcstoul(text, &end, 0);
    if (*end || !write_bytes(report, "{\"passed\":true,\"synthetic_fixture\":true}\r\n")) return 241;
    ExitProcess((DWORD)value);
    return 242;
}

static BOOL report_directory(const WCHAR *base, WCHAR *report) {
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    int count = 0;
    if (!path(pattern, base, L"TubeLiviano-offline-*")) return FALSE;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ++count;
            if (!path(report, base, data.cFileName)) count += 100;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count == 1;
}

static void test_case(const WCHAR *root, const WCHAR *script, const WCHAR *self,
                      const WCHAR *cmd, const WCHAR *label, DWORD expected, BOOL comma) {
    static const WCHAR *profiles[] = { L"144", L"240", L"360" };
    WCHAR base[MAX_PATH], stage[MAX_PATH], exe[MAX_PATH], batch[MAX_PATH];
    WCHAR media[MAX_PATH], file[MAX_PATH], console[MAX_PATH], reports[MAX_PATH];
    WCHAR command[4 * MAX_PATH], value[32];
    char text[8192];
    DWORD observed = STILL_ACTIVE, batch_exit = STILL_ACTIVE;
    int before = failures, reported_failures = -1;
    unsigned i;
    BOOL ok;
    CHECK(path(base, root, label));
    CHECK(CreateDirectoryW(base, NULL));
    CHECK(path(stage, base, L"batch fixture with spaces"));
    CHECK(CreateDirectoryW(stage, NULL));
    CHECK(path(exe, stage, L"TubeLiviano.exe"));
    CHECK(CopyFileW(self, exe, TRUE));
    CHECK(path(batch, stage, L"probar-offline.cmd"));
    CHECK(CopyFileW(script, batch, TRUE));
    CHECK(path(media, stage, L"media"));
    CHECK(CreateDirectoryW(media, NULL));
    for (i = 0; i < COUNT(profiles); ++i) {
        WCHAR name[32];
        wsprintfW(name, L"prueba-%ls.mp4", profiles[i]);
        CHECK(path(file, media, name));
        CHECK(write_bytes(file, "Synthetic fixture: existence check only; not actual media.\r\n"));
    }
    wsprintfW(value, L"0x%08lX", (unsigned long)expected);
    CHECK(SetEnvironmentVariableW(L"TL_BATCH_FIXTURE_EXIT", value));
    CHECK(SetEnvironmentVariableW(L"TEMP", base));
    CHECK(SetEnvironmentVariableW(L"TMP", base));
    CHECK(SetEnvironmentVariableW(L"ERRORLEVEL", NULL));
    CHECK(path(file, base, L"direct-probe.json"));
    _snwprintf(command, COUNT(command), L"\"%ls\" --self-test-media fixture --report \"%ls\"", exe, file);
    ok = run(exe, command, stage, &observed);
    CHECK(ok);
    CHECK(observed == expected);
    CHECK(path(console, base, L"console.txt"));
    _snwprintf(command, COUNT(command), L"\"%ls\" /d /s /c \"\"%ls\" <NUL >\"%ls\" 2>&1\"", cmd, batch, console);
    CHECK(run(cmd, command, stage, &batch_exit));
    CHECK(expected == 0 ? batch_exit == 0 : batch_exit != 0);
    ok = report_directory(base, reports);
    CHECK(ok);
    if (ok) {
        CHECK(path(file, reports, L"resultado.txt"));
        ok = read_text(file, text, sizeof(text));
        CHECK(ok);
        if (ok) {
            const char *line = strstr(text, "Fallos: ");
            if (line) reported_failures = atoi(line + strlen("Fallos: "));
            CHECK(reported_failures == (expected == 0 ? 0 : 3));
            CHECK(occurrences(text, "p: APROBADO") == (expected == 0 ? 3 : 0));
            CHECK(occurrences(text, "FALLO - proceso no exitoso") == (expected == 0 ? 0 : 3));
        }
        for (i = 0; i < COUNT(profiles); ++i) {
            WCHAR name[32];
            wsprintfW(name, L"%lsp.json", profiles[i]);
            CHECK(path(file, reports, name));
            CHECK(read_text(file, text, sizeof(text)));
            CHECK(strstr(text, "\"passed\":true") != NULL);
        }
    }
    printf("%s{\"case\":\"%ls\",\"fixture_exit_expected\":\"0x%08lX\","
           "\"fixture_exit_observed\":\"0x%08lX\",\"batch_exit\":%lu,"
           "\"reported_failures\":%d,\"passed\":%s}", comma ? ",\n" : "",
           label, (unsigned long)expected, (unsigned long)observed,
           (unsigned long)batch_exit, reported_failures, before == failures ? "true" : "false");
}

int wmain(int argc, WCHAR **argv) {
    WCHAR root[MAX_PATH], self[MAX_PATH], script[MAX_PATH], cmd[MAX_PATH];
    DWORD n;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc >= 2 && !lstrcmpW(argv[1], L"--self-test-media")) return fake_child(argc, argv);
    if (argc != 2) { fprintf(stderr, "Usage: batch_test.exe <absolute probar-offline.cmd>\n"); return 2; }
    n = GetFullPathNameW(argv[1], MAX_PATH, script, NULL);
    CHECK(n > 0 && n < MAX_PATH);
    n = GetModuleFileNameW(NULL, self, MAX_PATH);
    CHECK(n > 0 && n < MAX_PATH);
    n = GetSystemDirectoryW(cmd, MAX_PATH);
    CHECK(n > 0 && n + 9 < MAX_PATH);
    if (failures) return 2;
    lstrcatW(cmd, L"\\cmd.exe");
    CHECK(temporary_directory(root));
    if (failures) return 2;
    printf("{\"fixture\":\"synthetic approved JSON then explicit process exit\","
           "\"synthetic_media\":true,\"actual_video_tested\":false,\"cases\":[\n");
    test_case(root, script, self, cmd, L"zero", 0, FALSE);
    test_case(root, script, self, cmd, L"access-violation-status", 0xc0000005UL, TRUE);
    test_case(root, script, self, cmd, L"minus-one-status", 0xffffffffUL, TRUE);
    cleanup(root);
    printf("\n],\"checks\":%d,\"failures\":%d,\"passed\":%s}\n", checks, failures, failures ? "false" : "true");
    return failures ? 1 : 0;
}
