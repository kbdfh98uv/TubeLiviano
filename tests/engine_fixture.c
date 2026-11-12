/* Deliberately tiny extractor stand-in. It does not contact YouTube. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int has(int argc, WCHAR **argv, const WCHAR *s)
{
    int i;
    for (i = 1; i < argc; ++i) if (!wcscmp(argv[i], s)) return i;
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    const WCHAR *target = argc > 1 ? argv[argc - 1] : L"";
    int fmt, js;
    if (has(argc, argv, L"--hold")) { Sleep(30000); return 0; }
    if (has(argc, argv, L"-F")) return 90;
    if (has(argc, argv, L"--dump-json")) {
        if (!wcscmp(target,L"ytsearch12:six")) return 96;
        if (!wcscmp(target,L"ytsearch6:six") || !wcscmp(target,L"ytsearch12:limit")) {
            int i;
            for (i=0; i<12; ++i) puts("{\"id\":\"abcdefghijk\",\"title\":\"Limit\"}");
            return 0;
        }
        if (!wcscmp(target,L"ytsearch12:configuredtimeout")) { Sleep(35000); return 0; }
        if (!wcscmp(target,L"ytsearch12:nul")) {
            static const char payload[]="{\"id\":\"abcdefghijk\",\"title\":\"valid prefix\"}\n\0trailing garbage";
            fwrite(payload,1,sizeof(payload)-1,stdout); return 0;
        }
        if (!wcscmp(target,L"ytsearch12:large")) {
            int i;
            fputs("{\"id\":\"abcdefghijk\",\"title\":\"Large\",\"channel\":\"Canal\",\"duration\":96,\"extra\":\"",stdout);
            for(i=0;i<81920;++i)putchar('a');
            puts("\"}"); return 0;
        }
        if (!wcscmp(target,L"ytsearch12:loaderfail")) ExitProcess(0xC0000139UL);
        if (!wcscmp(target,L"ytsearch12:cleanenv")) {
            WCHAR value[32];
            if (GetEnvironmentVariableW(L"_MEIPASS2",value,32) ||
                GetEnvironmentVariableW(L"_PYI_ARCHIVE_FILE",value,32) ||
                GetEnvironmentVariableW(L"__COMPAT_LAYER",value,32) ||
                !GetEnvironmentVariableW(L"TL_KEEP_ME",value,32) || wcscmp(value,L"kept")) return 95;
        }
        if (!wcscmp(target, L"ytsearch12:hang")) { Sleep(30000); return 0; }
        if (!wcscmp(target, L"ytsearch12:spawnchild")) {
            WCHAR exe[MAX_PATH], command[MAX_PATH + 20];
            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            FILE *file;
            GetModuleFileNameW(NULL, exe, MAX_PATH);
            swprintf(command, MAX_PATH + 20, L"\"%ls\" --hold", exe);
            ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));
            if (!CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 80;
            file = fopen("child.pid", "w");
            if (!file) return 81;
            fprintf(file, "%lu", (unsigned long)pi.dwProcessId); fclose(file);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            Sleep(30000); return 0;
        }
        if (!wcscmp(target, L"ytsearch12:malformed")) { puts("{\"title\":\"broken}"); return 0; }
        if (!wcscmp(target, L"ytsearch12:missingid")) { puts("{\"title\":\"missing\"}"); return 0; }
        if (!wcscmp(target, L"ytsearch12:invalidid")) { puts("{\"id\":\"abcd?fghijk\",\"title\":\"bad\"}"); return 0; }
        if (!wcscmp(target, L"ytsearch12:empty")) return 0;
        if (!wcscmp(target, L"ytsearch12:error")) { fputs("ERROR: test extraction failed", stderr); return 1; }
        if (!wcscmp(target, L"ytsearch12:overflow")) {
            int i; for (i = 0; i < 1200000; ++i) putchar('x'); return 0;
        }
        if (wcsstr(target, L"comillas") && wcscmp(target, L"ytsearch12:comillas \" barra \\\\ fin\\")) return 91;
        puts("{\"id\":\"abcdefghijk\",\"title\":\"caf\\u00e9 \\ud83d\\ude80 \\\"ok\\\"\",\"channel\":\"Espa\\u00f1ol\",\"duration\":96.5,\"extra\":{\"title\":\"evil\"}}");
        puts("{\"id\":\"0123456789_\",\"title\":null,\"channel\":null,\"uploader\":\"Fallback\",\"duration\":null}");
        return 0;
    }
    if (!has(argc, argv, L"--get-url")) return 92;
    if (!wcscmp(target,L"https://fixture/configuredtimeout")) { Sleep(35000); return 0; }
    if (!wcscmp(target,L"https://fixture/hang")) { Sleep(30000); return 0; }
    if (!wcscmp(target,L"https://fixture/nativecrash")) ExitProcess(0xC0000005UL);
    if (!wcscmp(target,L"https://fixture/usageerror")) return 2;
    if (!wcscmp(target,L"https://fixture/loaderfail")) ExitProcess(0xC0000139UL);
    if (!wcscmp(target,L"https://fixture/nul")) {
        static const char payload[]="https://fixture/video\n\0file:///hidden";
        fwrite(payload,1,sizeof(payload)-1,stdout); return 0;
    }
    fmt = has(argc, argv, L"--format");
    js = has(argc, argv, L"--js-runtimes");
    if (!js || js + 1 >= argc || wcsncmp(argv[js + 1], L"quickjs:", 8) ||
        !wcsstr(argv[js + 1], L"\\runtimes\\qjs.exe")) return 94;
    if (!fmt || fmt + 1 >= argc || !wcsstr(argv[fmt + 1], L"[fps<=30]") ||
        !wcsstr(argv[fmt + 1], L"[vcodec^=avc1]") || !wcsstr(argv[fmt + 1], L"[acodec^=mp4a]")) return 93;
    if (!wcscmp(target, L"https://fixture/fallback") && !has(argc, argv, L"--check-formats")) return 1;
    if (!wcscmp(target, L"https://fixture/bad")) { puts("file:///c:/secret.mp4"); return 0; }
    if (!wcscmp(target, L"https://fixture/control")) { puts("https://fixture/video\tbad"); return 0; }
    if (!wcscmp(target, L"https://fixture/three")) {
        puts("https://fixture/one\nhttps://fixture/two\nhttps://fixture/three"); return 0;
    }
    if (!wcscmp(target, L"https://fixture/empty")) return 0;
    puts("https://fixture/video?x=1&y=2");
    if (!wcscmp(target, L"https://fixture/split")) puts("https://fixture/audio?x=3&y=4");
    return 0;
}
