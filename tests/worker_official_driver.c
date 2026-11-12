#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
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
#include "../src/engine_worker.h"
int wmain(int argc,WCHAR **argv) {
    Engine engine; EngineResult results[ENGINE_MAX_RESULTS]; EngineStreams streams;
    WCHAR error[768]; DWORD start,search_ms,resolve_ms,cleanup_ms,fresh_ms; int count=0,passed=1;
    FILE *report;
    if(argc!=3) return 2;
    ZeroMemory(&engine,sizeof(engine));
    if(wcslen(argv[1])>MAX_PATH-30) return 2;
    wcscpy(engine.base,argv[1]);
    _snwprintf(engine.exe,MAX_PATH,L"%ls\\engines\\modern\\yt-dlp.exe",engine.base);
    engine.os_major=10; engine.search_limit=6;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    start=GetTickCount();
    if(!engine_search(&engine,L"Caf\x00e9 fixture",NULL,results,&count,error,768)) {
        fwprintf(stderr,L"Search failure %d: %ls\n",engine.failure,error); return 1;
    }
    search_ms=GetTickCount()-start;
    passed=count==1 && !wcscmp(results[0].title,L"Caf\x00e9 \xd83c\xdfb5") && results[0].duration==96;
    passed=passed && !engine_worker_closed();
    start=GetTickCount();
    if(!engine_resolve(&engine,L"https://fixture.invalid/muxed",240,FALSE,NULL,&streams,error,768)) {
        fwprintf(stderr,L"Resolve failure %d: %ls\n",engine.failure,error); engine_worker_stop(); return 1;
    }
    resolve_ms=GetTickCount()-start;
    passed=passed && !strcmp(streams.video,"https://cdn.invalid/muxed-240.mp4") && !streams.audio[0] && resolve_ms<1000;
    start=GetTickCount();
    while(!engine_worker_closed() && GetTickCount()-start<8000) Sleep(10);
    cleanup_ms=GetTickCount()-start;
    passed=passed && engine_worker_closed();
    start=GetTickCount();
    if(!engine_resolve(&engine,L"https://fixture.invalid/muxed",240,FALSE,NULL,&streams,error,768)) {
        fwprintf(stderr,L"Fresh resolve failure %d: %ls\n",engine.failure,error); engine_worker_stop(); return 1;
    }
    fresh_ms=GetTickCount()-start;
    passed=passed && !strcmp(streams.video,"https://cdn.invalid/muxed-240.mp4") && fresh_ms>resolve_ms;
    start=GetTickCount();
    while(!engine_worker_closed() && GetTickCount()-start<8000) Sleep(10);
    passed=passed && engine_worker_closed();
    report=_wfopen(argv[2],L"wb");
    if(!report) return 3;
    fprintf(report,"{\"passed\":%s,\"frontend_bits\":%u,\"search_ms\":%lu,\"reused_resolve_ms\":%lu,\"cleanup_wait_ms\":%lu,\"fresh_resolve_ms\":%lu,\"offline_fixture\":true,\"live_youtube\":false}\n",
        passed ? "true" : "false",(unsigned)(sizeof(void*)*8),(unsigned long)search_ms,(unsigned long)resolve_ms,(unsigned long)cleanup_ms,(unsigned long)fresh_ms);
    if(fclose(report)!=0) return 3;
    printf("Official worker: %s; search %lu ms, reused resolve %lu ms, cleanup %lu ms, fresh resolve %lu ms\n",passed ? "PASS" : "FAIL",(unsigned long)search_ms,(unsigned long)resolve_ms,(unsigned long)cleanup_ms,(unsigned long)fresh_ms);
    return passed ? 0 : 1;
}
