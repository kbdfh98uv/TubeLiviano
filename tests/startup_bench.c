/* Fresh-process startup benchmark for the real player.c and generated SMPTE clip.
 * Compile at the same architecture as the staged VLC runtime, for example:
 *   i686-w64-mingw32-gcc -municode -std=c99 -Os -DUNICODE -D_UNICODE \
 *     -D__USE_MINGW_ANSI_STDIO=0 -Wall -Wextra -Werror \
 *     tests/startup_bench.c src/player.c -static-libgcc -luser32 -lgdi32 \
 *     -Wl,--no-insert-timestamp -o build/startup_bench.exe
 * Run: startup_bench.exe --base ABS_STAGE --url HTTP_URL_OR_ABS_CLIP \
 *     --report ABS_JSON [--cache-ms 450] [--prefetch-kib 128] [--threads 1]
 *     [--timeout-ms 15000] [--sample-ms 10]
 * Only generated colorful test clips are valid for the visible-frame probe.
 * Dummy audio verifies decoding, never physical sound or Atom performance.
 */
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
#include <stdlib.h>
#include <wchar.h>
#include "../src/player.h"

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static HWND video;
static BOOL cancelled;

static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    switch(message) {
        case WM_SIZE:
            if(video) MoveWindow(video,0,0,LOWORD(lp),HIWORD(lp),TRUE);
            return 0;
        case WM_CLOSE: cancelled=TRUE; return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
static void pump(void)
{
    MSG message;
    while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        if(message.message==WM_QUIT) cancelled=TRUE;
        else { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
static BOOL visible_pattern(HDC screen)
{
    RECT rect;
    int x,y,colored=0;
    if(!screen || !GetWindowRect(video,&rect) || IsIconic(GetParent(video))) return FALSE;
    /* Sample only the viewport, away from title, borders, OSD and letterbox.
     * Pure gray/black startup surfaces cannot satisfy these color differences. */
    for(y=1;y<=2;++y) for(x=1;x<=3;++x) {
        COLORREF pixel=GetPixel(screen,rect.left+(rect.right-rect.left)*x/4,
                               rect.top+(rect.bottom-rect.top)*(y+1)/5);
        int r,g,b,high,low;
        if(pixel==CLR_INVALID) continue;
        r=GetRValue(pixel); g=GetGValue(pixel); b=GetBValue(pixel);
        high=r>g ? r : g; if(b>high) high=b;
        low=r<g ? r : g; if(b<low) low=b;
        if(high>=150 && high-low>=100) ++colored;
    }
    return colored>=2;
}
static BOOL integer(const WCHAR *text,int *result)
{
    WCHAR *end;
    unsigned long value;
    if(!text || !*text || *text<L'0' || *text>L'9') return FALSE;
    value=wcstoul(text,&end,10);
    if(*end || value>60000) return FALSE;
    *result=(int)value; return TRUE;
}
int wmain(int argc,WCHAR **argv)
{
    const WCHAR *base=NULL,*media=NULL,*report=NULL;
    int timeout=15000,sample=10,resolve_delay=0,async_prepare=0,i,state=0,exit_code=1;
    int init_ms=-1,open_ms=-1,visible_ms=-1,video_ms=-1,audio_ms=-1,visible_bytes=-1;
    DWORD requested=0,opened=0,closed=0;
    WCHAR error[768]; char media8[24576];
    const char *failure="arguments";
    BOOL local=FALSE,passed=FALSE,started=FALSE,report_ok=FALSE;
    HDC screen=NULL;
    HWND window=NULL;
    WNDCLASSW cls;
    RECT client;
    FILE *output;
    PlayerStats stats,last={0};
    PlayerOptions options={450,128,1,80,FALSE};

    for(i=1;i<argc;i+=2) {
        if(i+1>=argc) goto usage;
        if(!wcscmp(argv[i],L"--base")) base=argv[i+1];
        else if(!wcscmp(argv[i],L"--url")) media=argv[i+1];
        else if(!wcscmp(argv[i],L"--report")) report=argv[i+1];
        else if(!wcscmp(argv[i],L"--cache-ms")) { if(!integer(argv[i+1],&options.cache_ms)) goto usage; }
        else if(!wcscmp(argv[i],L"--prefetch-kib")) { if(!integer(argv[i+1],&options.prefetch_kib)) goto usage; }
        else if(!wcscmp(argv[i],L"--threads")) { if(!integer(argv[i+1],&options.decoder_threads)) goto usage; }
        else if(!wcscmp(argv[i],L"--timeout-ms")) { if(!integer(argv[i+1],&timeout)) goto usage; }
        else if(!wcscmp(argv[i],L"--sample-ms")) { if(!integer(argv[i+1],&sample)) goto usage; }
        else if(!wcscmp(argv[i],L"--resolve-ms")) { if(!integer(argv[i+1],&resolve_delay) || resolve_delay>5000) goto usage; }
        else if(!wcscmp(argv[i],L"--async-prepare")) { if(!integer(argv[i+1],&async_prepare) || async_prepare>1) goto usage; }
        else goto usage;
    }
    if(!base || !media || !report || options.cache_ms<250 || options.cache_ms>3000 ||
       (options.prefetch_kib!=64 && options.prefetch_kib!=128 && options.prefetch_kib!=256) ||
       (options.decoder_threads!=1 && options.decoder_threads!=2) || timeout<1000 || sample<1 || sample>100)
        goto usage;
    if(!WideCharToMultiByte(CP_UTF8,0,media,-1,media8,sizeof(media8),NULL,NULL)) goto usage;
    local=_wcsnicmp(media,L"http://",7)!=0 && _wcsnicmp(media,L"https://",8)!=0;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    ZeroMemory(&cls,sizeof(cls)); cls.lpfnWndProc=procedure;
    cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"TubeLiviano.StartupBenchmark";
    cls.hCursor=LoadCursor(NULL,IDC_ARROW); cls.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    failure="window";
    if(!RegisterClassW(&cls)) goto finish;
    window=CreateWindowW(cls.lpszClassName,L"TubeLiviano startup benchmark",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,24,24,660,420,NULL,NULL,cls.hInstance,NULL);
    if(!window) goto finish;
    GetClientRect(window,&client);
    video=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_BLACKRECT,
        0,0,client.right,client.bottom,window,NULL,cls.hInstance,NULL);
    if(!video) goto finish;
    ShowWindow(window,SW_SHOWNORMAL); UpdateWindow(window); SetForegroundWindow(window);
    pump(); screen=GetDC(NULL);
    player_configure(&options);
    requested=GetTickCount(); failure="player_init";
#ifdef BENCH_SYNC_BASELINE
    if(async_prepare) goto usage;
#else
    if(async_prepare) {
        DWORD prepare_start=GetTickCount();
        if(!player_prepare(base,video,TRUE,error,COUNT(error))) goto finish;
        init_ms=(int)(GetTickCount()-prepare_start);
    }
#endif
    /* Explicit synthetic resolver delay; no hidden prewarming before request. */
    while(GetTickCount()-requested<(DWORD)resolve_delay && !cancelled) { pump(); Sleep(10); }
    if(!async_prepare) {
        DWORD init_start=GetTickCount();
        if (!player_init(base,video,TRUE,error,COUNT(error))) {
            fwprintf(stderr,L"player_init: %ls\n",error); goto finish;
        }
        init_ms=(int)(GetTickCount()-init_start);
    }
    failure="player_open";
    if(!player_open(media8,NULL,local,error,COUNT(error))) {
        fwprintf(stderr,L"player_open: %ls\n",error); goto finish;
    }
    opened=GetTickCount(); open_ms=(int)(opened-requested); started=TRUE;
    failure="timeout";
    while(GetTickCount()-requested<(DWORD)timeout && !cancelled) {
        int elapsed;
        pump(); elapsed=(int)(GetTickCount()-requested);
        state=player_state();
        if(player_stats(&stats)) {
            last=stats;
            if(video_ms<0 && stats.decoded_video>0) video_ms=elapsed;
            if(audio_ms<0 && stats.played_abuffers>0) audio_ms=elapsed;
        }
        if(visible_ms<0 && visible_pattern(screen)) {
            visible_ms=elapsed;
            /* VLC stats update in batches; zero is unknown, not proof of zero I/O. */
            if(last.read_bytes>0) visible_bytes=last.read_bytes;
        }
        if(visible_ms>=0 && last.decoded_video>0 && last.decoded_audio>0 &&
           last.displayed_pictures>0 && last.played_abuffers>0 && elapsed-visible_ms>=500) {
            passed=state!=7; failure=passed ? "none" : "player_error"; break;
        }
        if(state==7) { failure="player_error"; break; }
        if(state==6) { failure="ended_before_proof"; break; }
        Sleep((DWORD)sample);
    }
    if(cancelled) failure="cancelled";
finish:
    closed=GetTickCount();
    if(screen) ReleaseDC(NULL,screen);
    player_close();
    while(!player_closed() && GetTickCount()-closed<15000) { pump(); Sleep(10); }
    if(!player_closed()) { passed=FALSE; failure="shutdown_timeout"; }
    if(window) DestroyWindow(window);
    output=report ? _wfopen(report,L"wb") : NULL;
    if(output) {
        int written=fprintf(output,
            "{\"frontend_bits\":%u,\"complete\":true,\"passed\":%s,\"failure_stage\":\"%s\","
            "\"media_kind\":\"%s\",\"dummy_audio\":true,\"sample_ms\":%d,\"timeout_ms\":%d,"
            "\"player_init_ms\":%d,\"open_queued_ms\":%d,\"first_visible_ms\":%d,"
            "\"first_video_stats_ms\":%d,\"first_audio_stats_ms\":%d,\"first_visible_read_bytes\":%d,"
            "\"observation_ms\":%lu,\"shutdown_ms\":%lu,\"state\":%d,"
            "\"decoded_video\":%d,\"decoded_audio\":%d,\"displayed_pictures\":%d,"
            "\"played_audio_buffers\":%d,\"read_bytes\":%d,"
            "\"cache_ms\":%d,\"prefetch_kib\":%d,\"decoder_threads\":%d,\"resolve_delay_ms\":%d,\"async_prepare\":%s}\n",
            (unsigned)(sizeof(void*)*8),passed ? "true" : "false",failure,local ? "local" : "http",
            sample,timeout,init_ms,open_ms,visible_ms,video_ms,audio_ms,visible_bytes,
            (unsigned long)(started ? closed-requested : 0),(unsigned long)(GetTickCount()-closed),state,
            last.decoded_video,last.decoded_audio,last.displayed_pictures,last.played_abuffers,last.read_bytes,
            options.cache_ms,options.prefetch_kib,options.decoder_threads,resolve_delay,async_prepare ? "true":"false");
        report_ok=written>0;
        if(fclose(output)!=0) report_ok=FALSE;
    }
    exit_code=passed && report_ok ? 0 : 1;
    return exit_code;
usage:
    fputs("usage: startup_bench.exe --base ABS_STAGE --url HTTP_OR_ABS_GENERATED_CLIP --report ABS_JSON "
          "[--cache-ms 450] [--prefetch-kib 128] [--threads 1] [--timeout-ms 15000] [--sample-ms 10] [--resolve-ms 0] [--async-prepare 0]\n",stderr);
    return 2;
}
