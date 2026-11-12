#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "engine_process.h"
#include <string.h>
#include <wchar.h>
#define COMMAND_CAP ENGINE_COMMAND_CAP

/* Windows CommandLineToArgvW / MS CRT quoting. Do not invoke a shell. */
BOOL engine_command_arg(WCHAR *command, size_t *used, const WCHAR *arg)
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

/* A one-file extractor is an independent application. Never inherit another
 * PyInstaller application's extraction directory or Windows compatibility layer.
 * Keep proxy, certificates and all other user variables unchanged and private. */
WCHAR *engine_child_environment(void)
{
    WCHAR *original=GetEnvironmentStringsW(), *result, *dest;
    const WCHAR *line;
    size_t capacity=2;
    if (!original) return NULL;
    for (line=original; *line; line+=wcslen(line)+1) capacity+=wcslen(line)+1;
    result=(WCHAR *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,capacity*sizeof(WCHAR));
    if (!result) { FreeEnvironmentStringsW(original); return NULL; }
    dest=result;
    for (line=original; *line; line+=wcslen(line)+1) {
        size_t length=wcslen(line)+1;
        if (!_wcsnicmp(line,L"_PYI_",5) || !_wcsnicmp(line,L"_MEIPASS2=",10) ||
            !_wcsnicmp(line,L"__COMPAT_LAYER=",15)) continue;
        memcpy(dest,line,length*sizeof(WCHAR)); dest+=length;
    }
    *dest=0;
    FreeEnvironmentStringsW(original);
    return result;
}

