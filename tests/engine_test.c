#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "../src/engine.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failed = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); ++failed; } } while (0)

typedef struct { WCHAR base[MAX_PATH]; HANDLE event; HANDLE child; } CancelTask;
static DWORD WINAPI cancel_child(LPVOID ptr)
{
    CancelTask *task = (CancelTask *)ptr;
    WCHAR file[MAX_PATH];
    DWORD start = GetTickCount();
    wcscpy(file, task->base); wcscat(file, L"\\child.pid");
    while ((DWORD)(GetTickCount() - start) < 5000) {
        FILE *f = _wfopen(file, L"r");
        if (f) {
            unsigned long pid = 0;
            int read = fscanf(f, "%lu", &pid); fclose(f);
            if (read == 1 && pid) {
                task->child = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
                break;
            }
        }
        Sleep(20);
    }
    SetEvent(task->event);
    return 0;
}

int wmain(void)
{
    Engine engine, selected;
    EngineResult result[ENGINE_MAX_RESULTS];
    EngineStreams streams;
    WCHAR error[2048], exe[MAX_PATH], temporary[MAX_PATH], folder[MAX_PATH], file[MAX_PATH];
    WCHAR *slash;
    HANDLE event, thread;
    int count, i;
    DWORD start;
    CancelTask task;
    const WCHAR *bad[] = {L"malformed", L"missingid", L"invalidid", L"empty", L"error", L"overflow"};
    const WCHAR *bad_url[] = {L"https://fixture/bad", L"https://fixture/control", L"https://fixture/three", L"https://fixture/empty"};
    GetModuleFileNameW(NULL, exe, MAX_PATH); slash = wcsrchr(exe, L'\\');
    if (!slash) return 2;
    wcscpy(slash + 1, L"engine_fixture.exe");
    GetTempPathW(MAX_PATH, temporary);
    swprintf(folder, MAX_PATH, L"%lsTubeLiviano engine %lu", temporary, (unsigned long)GetCurrentProcessId());
    CHECK(CreateDirectoryW(folder, NULL));
    ZeroMemory(&engine, sizeof(engine)); wcscpy(engine.exe, exe); wcscpy(engine.base, folder);
    event = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(event != NULL);

    /* Install fake files in BOTH engine locations to validate real OS routing. */
    swprintf(file, MAX_PATH, L"%ls\\engines", folder); CHECK(CreateDirectoryW(file, NULL));
    swprintf(file, MAX_PATH, L"%ls\\engines\\xp", folder); CHECK(CreateDirectoryW(file, NULL));
    wcscat(file, L"\\yt-dlp.exe"); CHECK(CopyFileW(exe, file, TRUE));
    swprintf(file, MAX_PATH, L"%ls\\engines\\modern", folder); CHECK(CreateDirectoryW(file, NULL));
    wcscat(file, L"\\yt-dlp.exe"); CHECK(CopyFileW(exe, file, TRUE));
    CHECK(engine_init(&selected, folder, error, 2048));
    CHECK(wcsstr(selected.exe, selected.xp ? L"\\xp\\" : L"\\modern\\") != NULL);
    wprintf(L"Selected engine: %ls\n", selected.exe);
    wcscpy(file, selected.exe);
    CHECK(DeleteFileW(file));
    CHECK(!engine_init(&selected, folder, error, 2048)); /* Never substitute the other OS engine. */
    CHECK(CopyFileW(exe, file, TRUE));

    CHECK(engine_search(&engine, L"comillas \" barra \\\\ fin\\", event, result, &count, error, 2048));
    CHECK(count == 2);
    CHECK(!wcscmp(result[0].title, L"caf\u00e9 \xd83d\xde80 \"ok\""));
    CHECK(!wcscmp(result[0].channel, L"Espa\u00f1ol"));
    CHECK(result[0].duration == 96);
    CHECK(!wcscmp(result[1].title, L"0123456789_"));
    CHECK(!wcscmp(result[1].channel, L"Fallback"));
    CHECK(result[1].duration == -1);
    for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); ++i) {
        CHECK(!engine_search(&engine, bad[i], event, result, &count, error, 2048));
        CHECK(count == 0 && error[0]);
    }
    for (i = 0; i < 3; ++i) {
        int height = i == 0 ? 144 : i == 1 ? 240 : 360;
        CHECK(engine_resolve(&engine, L"https://fixture/muxed", height, FALSE, event, &streams, error, 2048));
        CHECK(!strcmp(streams.video, "https://fixture/video?x=1&y=2") && !streams.audio[0]);
    }
    CHECK(engine_resolve(&engine, L"https://fixture/split", 240, FALSE, event, &streams, error, 2048));
    CHECK(!strcmp(streams.audio, "https://fixture/audio?x=3&y=4"));
    CHECK(!engine_resolve(&engine, L"https://fixture/fallback", 240, FALSE, event, &streams, error, 2048));
    CHECK(engine_resolve(&engine, L"https://fixture/fallback", 240, TRUE, event, &streams, error, 2048));
    CHECK(!engine_resolve(&engine, L"https://fixture/muxed", 720, FALSE, event, &streams, error, 2048));
    CHECK(!engine_resolve(&engine, L"file:///bad", 240, FALSE, event, &streams, error, 2048));
    for (i = 0; i < (int)(sizeof(bad_url) / sizeof(bad_url[0])); ++i)
        CHECK(!engine_resolve(&engine, bad_url[i], 240, FALSE, event, &streams, error, 2048));
    SetEvent(event);
    start = GetTickCount();
    CHECK(!engine_search(&engine, L"hang", event, result, &count, error, 2048));
    CHECK((DWORD)(GetTickCount() - start) < 2000);
    ResetEvent(event);

    ZeroMemory(&task, sizeof(task)); wcscpy(task.base, folder); task.event = event;
    thread = CreateThread(NULL, 0, cancel_child, &task, 0, NULL); CHECK(thread != NULL);
    start = GetTickCount();
    CHECK(!engine_search(&engine, L"spawnchild", event, result, &count, error, 2048));
    CHECK(WaitForSingleObject(thread, 6000) == WAIT_OBJECT_0);
    CHECK((DWORD)(GetTickCount() - start) < 7000);
    CHECK(task.child != NULL);
    if (task.child) {
        CHECK(WaitForSingleObject(task.child, 2000) == WAIT_OBJECT_0);
        CloseHandle(task.child);
    }
    CloseHandle(thread); CloseHandle(event);
    /* This fixture directory only contains files created by this test. */
    swprintf(file, MAX_PATH, L"%ls\\child.pid", folder); DeleteFileW(file);
    swprintf(file, MAX_PATH, L"%ls\\engines\\xp\\yt-dlp.exe", folder); DeleteFileW(file);
    swprintf(file, MAX_PATH, L"%ls\\engines\\modern\\yt-dlp.exe", folder); DeleteFileW(file);
    swprintf(file, MAX_PATH, L"%ls\\engines\\xp", folder); RemoveDirectoryW(file);
    swprintf(file, MAX_PATH, L"%ls\\engines\\modern", folder); RemoveDirectoryW(file);
    swprintf(file, MAX_PATH, L"%ls\\engines", folder); RemoveDirectoryW(file);
    RemoveDirectoryW(folder);
    printf("%d checks; %d failed\n", checks, failed);
    return failed ? 1 : 0;
}
