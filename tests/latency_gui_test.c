/* Native GUI integration and a controlled two-phase latency model.
 * Run: latency_gui_test.exe APP.exe FAKE_VLC.dll [--baseline] [--report ABS_JSON]
 * Build: MinGW gcc -municode -std=c99 -Os -Wall -Wextra -Werror
 *        tests/latency_gui_test.c -static-libgcc -o latency_gui_test.exe
 * The extractor and VLC are fixtures. Timings are not real YouTube/Atom data.
 */
#define _WIN32_WINNT 0x0501
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/ui.h"

#define CAP(a) (sizeof(a)/sizeof((a)[0]))
#define ENGINE_DELAY_MS 900
#define INIT_DELAY_MS 1200
static const WCHAR youtube_url[]=L"https://www.youtube.com/watch?v=abcdefghijk";
static HWND app;
static DWORD app_pid;
static int checks,failures,blocked_probes;
static DWORD worst_probe;
static WCHAR root[MAX_PATH],stage[MAX_PATH],engine_log[MAX_PATH],fail_play[MAX_PATH],hang_play[MAX_PATH],slow_audio[MAX_PATH];
static DWORD first_ms,repeat_ms,retry_ms,recovery_ms,cached_timeout_ms,cancel_ms,close_ms;
static int first_calls,repeat_calls,retry_calls,recovery_calls,timeout_calls;
static BOOL baseline;
#define CHECK(c) do { ++checks; if(!(c)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#c); } } while(0)

static BOOL path(WCHAR *out,const WCHAR *base,const WCHAR *name) {
    int n=_snwprintf(out,MAX_PATH,L"%ls\\%ls",base,name);
    return n>=0 && n<MAX_PATH;
}
static BOOL empty_file(const WCHAR *name) {
    HANDLE h=CreateFileW(name,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    return h!=INVALID_HANDLE_VALUE && CloseHandle(h);
}
static BOOL has(int argc,WCHAR **argv,const WCHAR *name) {
    int i; for(i=1;i<argc;++i)if(!lstrcmpW(argv[i],name))return TRUE;
    return FALSE;
}
static int fake_engine(int argc,WCHAR **argv) {
    WCHAR file[MAX_PATH],delay[24]; DWORD n,ms=ENGINE_DELAY_MS;
    FILE *output;
    n=GetEnvironmentVariableW(L"TL_LATENCY_ENGINE_LOG",file,MAX_PATH);
    if(!n || n>=MAX_PATH)return 3;
    output=_wfopen(file,L"ab");
    if(!output)return 4;
    fputs(has(argc,argv,L"--check-formats") ? "probe\n" : "resolve\n",output);
    if(fclose(output))return 5;
    if(GetEnvironmentVariableW(L"TL_LATENCY_ENGINE_MS",delay,CAP(delay)))ms=(DWORD)_wtoi(delay);
    Sleep(ms);
    puts("https://fixture/video");
    return 0;
}
static int calls(BOOL *probed) {
    FILE *input=_wfopen(engine_log,L"rb"); char data[2048];size_t n;int count=0;size_t i;
    if(probed)*probed=FALSE;
    if(!input)return 0;
    n=fread(data,1,sizeof(data)-1,input);fclose(input);data[n]=0;
    for(i=0;i<n;++i)if(data[i]=='\n')++count;
    if(probed)*probed=strstr(data,"probe")!=NULL;
    return count;
}
static BOOL CALLBACK find_app(HWND h,LPARAM unused) {
    WCHAR name[64];DWORD pid;(void)unused;
    GetWindowThreadProcessId(h,&pid);GetClassNameW(h,name,CAP(name));
    if(pid==app_pid && !lstrcmpW(name,L"TubeLiviano05")){app=h;return FALSE;}
    return TRUE;
}
static BOOL await_app(const PROCESS_INFORMATION *process) {
    DWORD start=GetTickCount();app=NULL;
    do {
        EnumWindows(find_app,0);
        if(app && GetDlgItem(app,RESULTS_NAV) && IsWindowVisible(GetDlgItem(app,PASTE)) &&
           !IsWindowVisible(GetDlgItem(app,PLAY)))return TRUE;
        if(WaitForSingleObject(process->hProcess,0)==WAIT_OBJECT_0)return FALSE;
        Sleep(10);
    }while(GetTickCount()-start<7000);
    return FALSE;
}
static BOOL launch(PROCESS_INFORMATION *process) {
    WCHAR exe[MAX_PATH],command[MAX_PATH+4];STARTUPINFOW startup;
    if(!path(exe,stage,L"TubeLiviano.exe"))return FALSE;
    _snwprintf(command,CAP(command),L"\"%ls\"",exe);
    ZeroMemory(&startup,sizeof(startup));startup.cb=sizeof(startup);ZeroMemory(process,sizeof(*process));
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,stage,&startup,process))return FALSE;
    app_pid=process->dwProcessId;return TRUE;
}
static BOOL close_app(PROCESS_INFORMATION *process,DWORD timeout) {
    BOOL done;DWORD code=99;
    if(app && IsWindow(app))PostMessageW(app,WM_CLOSE,0,0);
    done=WaitForSingleObject(process->hProcess,timeout)==WAIT_OBJECT_0;
    if(!done){TerminateProcess(process->hProcess,99);WaitForSingleObject(process->hProcess,2000);}
    GetExitCodeProcess(process->hProcess,&code);
    CloseHandle(process->hThread);CloseHandle(process->hProcess);ZeroMemory(process,sizeof(*process));app=NULL;
    return done && code==0;
}
static BOOL click(int id,DWORD timeout) {
    DWORD_PTR result=0;
    return SendMessageTimeoutW(GetDlgItem(app,id),BM_CLICK,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,timeout,&result)!=0;
}
static BOOL responsive(void) {
    DWORD before=GetTickCount(),elapsed;DWORD_PTR result=0;
    BOOL ok=SendMessageTimeoutW(app,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,200,&result)!=0;
    elapsed=GetTickCount()-before;if(elapsed>worst_probe)worst_probe=elapsed;
    if(!ok)++blocked_probes;
    return ok;
}
static BOOL wait_ready(DWORD limit,BOOL probe) {
    DWORD start=GetTickCount();
    do {
        if(IsWindowEnabled(GetDlgItem(app,PAUSE)))return TRUE;
        if(probe)responsive();
        Sleep(10);
    }while(GetTickCount()-start<limit);
    return FALSE;
}
static BOOL start_url(void) {
    if(!SetWindowTextW(GetDlgItem(app,QUERY),youtube_url))return FALSE;
    return click(SEARCH,3000);
}
static void stop(void) {
    CHECK(click(CANCEL,1500));
    CHECK(!IsWindowEnabled(GetDlgItem(app,PAUSE)));
}
static void normal_flow(void) {
    PROCESS_INFORMATION process;DWORD before;BOOL probed;
    CHECK(launch(&process));if(!process.hProcess)return;
    CHECK(await_app(&process));if(!app){CHECK(close_app(&process,3000));return;}
    before=GetTickCount();CHECK(start_url());CHECK(wait_ready(7000,TRUE));first_ms=GetTickCount()-before;
    first_calls=calls(&probed);CHECK(first_calls==1);CHECK(!probed);
    CHECK(first_ms>=INIT_DELAY_MS-150 && first_ms<6500);
    if(!baseline)CHECK(blocked_probes==0);
    /* Fake audio/video counters become positive after100ms; allow twoUIticks. */
    Sleep(350);stop();
    before=GetTickCount();CHECK(start_url());CHECK(wait_ready(6500,FALSE));repeat_ms=GetTickCount()-before;
    repeat_calls=calls(&probed);CHECK(!probed);
    if(baseline){CHECK(repeat_calls==2);CHECK(repeat_ms>=ENGINE_DELAY_MS-100);}
    else {CHECK(repeat_calls==1);CHECK(repeat_ms<600);}
    Sleep(350);stop();
    before=GetTickCount();CHECK(click(RETRY,1500));CHECK(wait_ready(6500,FALSE));retry_ms=GetTickCount()-before;
    retry_calls=calls(&probed);CHECK(retry_calls==repeat_calls+1);CHECK(!probed);
    if(!baseline) {
        Sleep(350);stop();CHECK(empty_file(fail_play));
        before=GetTickCount();CHECK(start_url());CHECK(wait_ready(7000,FALSE));recovery_ms=GetTickCount()-before;
        recovery_calls=calls(&probed);CHECK(recovery_calls==retry_calls+1);CHECK(!probed);
        CHECK(GetFileAttributesW(fail_play)==INVALID_FILE_ATTRIBUTES);
        /* A cached URL that stalls must refresh after the bounded6s proof
         * deadline, rather than adding the normal25s stream watchdog. */
        Sleep(350);stop();CHECK(empty_file(hang_play));
        before=GetTickCount();CHECK(start_url());CHECK(wait_ready(12000,FALSE));
        cached_timeout_ms=GetTickCount()-before;
        timeout_calls=calls(&probed);CHECK(timeout_calls==recovery_calls+1);CHECK(!probed);
        CHECK(cached_timeout_ms>=5800 && cached_timeout_ms<12000);
        CHECK(GetFileAttributesW(hang_play)==INVALID_FILE_ATTRIBUTES);
        /* A visible cached video with a delayed audio slave must not be
         * interrupted by the six-second no-picture recovery deadline. */
        Sleep(350);stop();CHECK(empty_file(slow_audio));
        CHECK(start_url());CHECK(wait_ready(2000,FALSE));Sleep(6800);
        CHECK(calls(&probed)==timeout_calls);CHECK(!probed);
        CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
        CHECK(GetFileAttributesW(slow_audio)==INVALID_FILE_ATTRIBUTES);
    }
    CHECK(close_app(&process,7000));
}
static void slow_bootstrap_lifecycle(void) {
    PROCESS_INFORMATION process;DWORD before;DWORD_PTR result=0;
    SetEnvironmentVariableW(L"TL_FAKE_VLC_INIT_MS",L"1800");
    SetEnvironmentVariableW(L"TL_LATENCY_ENGINE_MS",L"100");
    CHECK(launch(&process));if(!process.hProcess)return;
    CHECK(await_app(&process));if(!app){CHECK(close_app(&process,3000));return;}
    CHECK(start_url());Sleep(300);
    CHECK(!IsWindowEnabled(GetDlgItem(app,PAUSE)));
    before=GetTickCount();CHECK(click(CANCEL,200));cancel_ms=GetTickCount()-before;
    CHECK(cancel_ms<200);CHECK(responsive());
    Sleep(1900);
    CHECK(!IsWindowEnabled(GetDlgItem(app,PAUSE)));
    CHECK(!IsWindowEnabled(GetDlgItem(app,FULLSCREEN)));
    CHECK(close_app(&process,6000));

    CHECK(launch(&process));if(!process.hProcess)return;
    CHECK(await_app(&process));if(!app){CHECK(close_app(&process,3000));return;}
    CHECK(start_url());Sleep(300);
    before=GetTickCount();
    CHECK(SendMessageTimeoutW(app,WM_CLOSE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,200,&result)!=0);
    close_ms=GetTickCount()-before;CHECK(close_ms<200);
    CHECK(close_app(&process,6000));
    SetEnvironmentVariableW(L"TL_FAKE_VLC_INIT_MS",L"1200");
    SetEnvironmentVariableW(L"TL_LATENCY_ENGINE_MS",L"900");
}
static BOOL create_stage(const WCHAR *application,const WCHAR *library,const WCHAR *self) {
    WCHAR temp[MAX_PATH],name[MAX_PATH];const WCHAR *dirs[]={L"vlc",L"engines",L"engines\\modern",L"engines\\xp",L"media"};unsigned i;
    DWORD n=GetTempPathW(MAX_PATH,temp);BOOL ok=n>0 && n<MAX_PATH;
    CHECK(ok);if(!ok)return FALSE;
    ok=GetTempFileNameW(temp,L"tll",0,root)!=0;CHECK(ok);if(!ok)return FALSE;
    ok=DeleteFileW(root);CHECK(ok);if(!ok)return FALSE;
    ok=CreateDirectoryW(root,NULL);CHECK(ok);if(!ok)return FALSE;
    lstrcpynW(stage,root,MAX_PATH);
    CHECK(path(name,stage,L"TubeLiviano.exe"));CHECK(CopyFileW(application,name,TRUE));
    for(i=0;i<CAP(dirs);++i){CHECK(path(name,stage,dirs[i]));CHECK(CreateDirectoryW(name,NULL));}
    CHECK(path(name,stage,L"vlc\\libvlc.dll"));CHECK(CopyFileW(library,name,TRUE));
    CHECK(path(name,stage,L"vlc\\libvlccore.dll"));CHECK(CopyFileW(library,name,TRUE));
    CHECK(path(name,stage,L"engines\\modern\\yt-dlp.exe"));CHECK(CopyFileW(self,name,TRUE));
    CHECK(path(name,stage,L"engines\\xp\\yt-dlp.exe"));CHECK(CopyFileW(self,name,TRUE));
    CHECK(path(name,stage,L"media\\prueba-240.mp4"));CHECK(empty_file(name));
    CHECK(path(engine_log,stage,L"engine-calls.txt"));CHECK(path(fail_play,stage,L"fail-play.once"));
    CHECK(path(hang_play,stage,L"hang-play.once"));
    CHECK(path(slow_audio,stage,L"slow-audio.once"));
    SetEnvironmentVariableW(L"TL_LATENCY_ENGINE_LOG",engine_log);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_PLAY_FILE",fail_play);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_HANG_PLAY_FILE",hang_play);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_SLOW_AUDIO_FILE",slow_audio);
    return failures==0;
}
static void cleanup(void) {
    const WCHAR *files[]={L"TubeLiviano.exe",L"TubeLiviano.ini",L"vlc\\libvlc.dll",L"vlc\\libvlccore.dll",L"engines\\modern\\yt-dlp.exe",L"engines\\xp\\yt-dlp.exe",L"media\\prueba-240.mp4",L"engine-calls.txt",L"fail-play.once",L"hang-play.once",L"slow-audio.once"};
    const WCHAR *dirs[]={L"media",L"engines\\modern",L"engines\\xp",L"engines",L"vlc"};WCHAR name[MAX_PATH];unsigned i;
    if(!root[0] || !stage[0])return;
    for(i=0;i<CAP(files);++i)if(path(name,stage,files[i]))DeleteFileW(name);
    for(i=0;i<CAP(dirs);++i)if(path(name,stage,dirs[i]))RemoveDirectoryW(name);
    RemoveDirectoryW(root);
}
static BOOL write_report(const WCHAR *name) {
    FILE *output;int written;BOOL ok;
    if(!name)return TRUE;
    output=_wfopen(name,L"wb");if(!output)return FALSE;
    written=fprintf(output,"{\"complete\":true,\"passed\":%s,\"baseline\":%s,\"frontend_bits\":%u,"
        "\"synthetic_media\":true,\"live_youtube\":false,\"real_vlc\":false,"
        "\"engine_delay_ms\":900,\"vlc_init_delay_ms\":1200,\"first_ready_ms\":%lu,"
        "\"repeat_ready_ms\":%lu,\"retry_ready_ms\":%lu,\"cache_recovery_ms\":%lu,"
        "\"cached_timeout_refresh_ms\":%lu,"
        "\"cancel_request_ms\":%lu,\"close_request_ms\":%lu,\"blocked_ui_probes\":%d,"
        "\"worst_ui_probe_ms\":%lu,\"engine_calls_first\":%d,\"engine_calls_repeat\":%d,"
        "\"engine_calls_retry\":%d,\"engine_calls_recovery\":%d,\"engine_calls_timeout_refresh\":%d,"
        "\"checks\":%d,\"failures\":%d}\n",
        failures?"false":"true",baseline?"true":"false",(unsigned)(sizeof(void*)*8),
        (unsigned long)first_ms,(unsigned long)repeat_ms,(unsigned long)retry_ms,(unsigned long)recovery_ms,
        (unsigned long)cached_timeout_ms,(unsigned long)cancel_ms,(unsigned long)close_ms,blocked_probes,(unsigned long)worst_probe,
        first_calls,repeat_calls,retry_calls,recovery_calls,timeout_calls,checks,failures);
    ok=written>0;if(fclose(output))ok=FALSE;return ok;
}
int wmain(int argc,WCHAR **argv) {
    const WCHAR *report=NULL;WCHAR self[MAX_PATH];int i;
    if(has(argc,argv,L"--get-url"))return fake_engine(argc,argv);
    if(argc<3){puts("usage: latency_gui_test.exe APP.exe FAKE_VLC.dll [--baseline] [--report ABS_JSON]");return 2;}
    for(i=3;i<argc;++i) {
        if(!lstrcmpW(argv[i],L"--baseline"))baseline=TRUE;
        else if(!lstrcmpW(argv[i],L"--report") && i+1<argc)report=argv[++i];
        else return 2;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_INIT_MS",L"1200");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_STOP_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_RELEASE_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_ERROR_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",L"0");
    SetEnvironmentVariableW(L"TL_LATENCY_ENGINE_MS",L"900");
    CHECK(GetModuleFileNameW(NULL,self,MAX_PATH)>0);
    if(create_stage(argv[1],argv[2],self)){normal_flow();if(!baseline)slow_bootstrap_lifecycle();}
    CHECK(write_report(report));cleanup();
    puts("Synthetic extractor/VLC only: controlled phase model, not real YouTube or hardware latency.");
    printf("Latency GUI: %d checks; %d failures\n",checks,failures);
    return failures?1:0;
}
