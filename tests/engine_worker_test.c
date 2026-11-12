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
#include <string.h>
#include <wchar.h>
#include <fcntl.h>
#include <io.h>
#include "../src/engine_worker.h"

static int checks, failed;
#define CHECK(x) do { ++checks; if(!(x)) { ++failed; printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
static WCHAR stage[MAX_PATH], logpath[MAX_PATH], self[MAX_PATH];
static Engine engine;
static void path(WCHAR *out,const WCHAR *suffix) { _snwprintf(out,MAX_PATH,L"%ls\\%ls",stage,suffix); }
static void logline(const char *kind) {
    WCHAR file[MAX_PATH]; FILE *out;
    if(!GetEnvironmentVariableW(L"TL_WORKER_LOG",file,MAX_PATH)) return;
    out=_wfopen(file,L"ab");
    if(out) { fprintf(out,"%s %lu\n",kind,(unsigned long)GetCurrentProcessId()); fclose(out); }
}
static void reply(const char *kind,const char *text) {
    printf("TLW1 %s %u\n",kind,(unsigned)strlen(text));
    fwrite(text,1,strlen(text),stdout); putchar('\n'); fflush(stdout);
}
static int fixture(void) {
    WCHAR mode[64]; char *line=malloc(65537);
    _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stdin),_O_BINARY);
    GetEnvironmentVariableW(L"TL_WORKER_MODE",mode,64);
    logline("BOOT");
    if(GetEnvironmentVariableW(L"_PYI_ARCHIVE_FILE",NULL,0) ||
       GetEnvironmentVariableW(L"_MEIPASS2",NULL,0) || GetEnvironmentVariableW(L"__COMPAT_LAYER",NULL,0)) return 7;
    if(!wcscmp(mode,L"unsupported")) return 2;
    if(!wcscmp(mode,L"loader")) ExitProcess(0xC0000139UL);
    if(!wcscmp(mode,L"boot-hang")) Sleep(INFINITE);
    if(!wcscmp(mode,L"noisy")) {
        int i; for(i=0;i<40000;++i) fputs("noise",stderr); fflush(stderr);
    }
    puts("TLW1 READY"); fflush(stdout);
    if(!wcscmp(mode,L"no-read")) { logline("READY"); Sleep(INFINITE); }
    while(fgets(line,65537,stdin)) {
        BOOL resolve=strstr(line,"\"op\":\"resolve\"")!=NULL;
        logline(resolve ? "RESOLVE" : "SEARCH");
        if(!wcscmp(mode,L"hang") || !wcscmp(mode,L"descendant")) {
            if(!wcscmp(mode,L"descendant")) {
                WCHAR executable[MAX_PATH],command[MAX_PATH+64];
                STARTUPINFOW startup; PROCESS_INFORMATION process;
                GetModuleFileNameW(NULL,executable,MAX_PATH);
                _snwprintf(command,MAX_PATH+64,L"\"%ls\" --fixture-descendant",executable);
                ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
                if(CreateProcessW(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process)) {
                    CloseHandle(process.hThread); CloseHandle(process.hProcess);
                }
            }
            Sleep(INFINITE);
        }
        if(!wcscmp(mode,L"partial")) { fputs("TLW1 OK 100\nx",stdout); fflush(stdout); Sleep(INFINITE); }
        if(!wcscmp(mode,L"bad")) { puts("TLW1 OK 999999999999"); fflush(stdout); Sleep(INFINITE); }
        if(!wcscmp(mode,L"nul")) { fwrite("TLW1 OK 3\na\0b\n",1,14,stdout); fflush(stdout); Sleep(INFINITE); }
        if(!wcscmp(mode,L"error")) { reply("ERROR","Fallo extractor de prueba"); break; }
        if(resolve) {
            reply("OK",!wcscmp(mode,L"split") ? "https://cdn.invalid/video\nhttps://cdn.invalid/audio\n" : "https://cdn.invalid/video\n");
            Sleep(700); break; /* Model one-file cleanup AFTER useful output. */
        }
        reply("OK","{\"id\":\"abcdefghijk\",\"title\":\"Caf\\u00e9\",\"channel\":\"Test\",\"duration\":96}\n");
    }
    free(line); return 0;
}
static void mode(const WCHAR *value) { SetEnvironmentVariableW(L"TL_WORKER_MODE",value); }
static BOOL wait_closed(DWORD timeout) {
    DWORD start=GetTickCount();
    while(!engine_worker_closed() && GetTickCount()-start<timeout) Sleep(10);
    return engine_worker_closed();
}
static void stop(void) { engine_worker_stop(); CHECK(wait_closed(2500)); }
static int count(const char *word,DWORD *pid) {
    FILE *input=_wfopen(logpath,L"rb"); char line[100],kind[50]; unsigned long number;
    int n=0;
    if(!input) return 0;
    while(fgets(line,sizeof(line),input)) if(sscanf(line,"%49s %lu",kind,&number)==2 && !strcmp(word,kind)) { ++n; if(pid) *pid=(DWORD)number; }
    fclose(input); return n;
}
static void resetlog(void) { DeleteFileW(logpath); }
static EngineWorkerResult search(HANDLE cancel,DWORD timeout,char **output,WCHAR *error) {
    return engine_worker_search(&engine,L"Cafe \"\\ \x00e9",6,cancel,timeout,output,error,768);
}
static void free_output(char **output) { if(*output) HeapFree(GetProcessHeap(),0,*output); *output=NULL; }
typedef struct { HANDLE cancel; Engine result; WCHAR error[768]; char *output; BOOL long_url; } Async;
static DWORD WINAPI async_call(void *data) {
    Async *a=data;
    a->result=engine;
    if(a->long_url) {
        WCHAR *url=calloc(8193,sizeof(WCHAR)); size_t i;
        wcscpy(url,L"https://fixture.invalid/");
        for(i=wcslen(url);i<8192;++i) url[i]=L'"';
        engine_worker_resolve(&a->result,url,240,FALSE,a->cancel,2000,&a->output,a->error,768);
        free(url);
    } else engine_worker_search(&a->result,L"test",6,a->cancel,1500,&a->output,a->error,768);
    return 0;
}
static void cancellation(const WCHAR *test_mode,BOOL stop_api,BOOL long_url) {
    Async a; HANDLE thread,descendant=NULL; DWORD start,pid=0;
    ZeroMemory(&a,sizeof(a)); a.cancel=CreateEventW(NULL,TRUE,FALSE,NULL); a.long_url=long_url;
    resetlog(); mode(test_mode);
    thread=CreateThread(NULL,0,async_call,&a,0,NULL); CHECK(thread!=NULL);
    start=GetTickCount();
    while(GetTickCount()-start<1000 && !count(long_url ? "READY" : "SEARCH",NULL)) Sleep(10);
    if(!wcscmp(test_mode,L"descendant")) {
        start=GetTickCount(); while(GetTickCount()-start<1000 && !count("DESCENDANT",&pid)) Sleep(10);
        CHECK(pid!=0); if(pid) descendant=OpenProcess(SYNCHRONIZE,FALSE,pid);
        CHECK(descendant!=NULL);
    }
    start=GetTickCount();
    if(stop_api) engine_worker_stop(); else SetEvent(a.cancel);
    CHECK(WaitForSingleObject(thread,1800)==WAIT_OBJECT_0);
    CHECK(GetTickCount()-start<1500);
    CHECK(a.result.failure==ENGINE_FAILURE_CANCELLED);
    CHECK(a.output==NULL);
    CHECK(wait_closed(2000));
    if(descendant) { CHECK(WaitForSingleObject(descendant,1000)==WAIT_OBJECT_0); CloseHandle(descendant); }
    CloseHandle(thread); CloseHandle(a.cancel);
}
int wmain(int argc,WCHAR **argv) {
    WCHAR error[768],temp[MAX_PATH],folder[MAX_PATH]; char *out=NULL;
    DWORD before,pid=0,search_pid=0,resolve_pid=0;
    int i;
    for(i=1;i<argc;++i) {
        if(!wcscmp(argv[i],L"--fixture-descendant")) { logline("DESCENDANT"); Sleep(INFINITE); }
        if(!wcscmp(argv[i],L"tubeliviano:worker")) return fixture();
    }
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    CHECK(GetModuleFileNameW(NULL,self,MAX_PATH)>0);
    CHECK(GetTempPathW(MAX_PATH,temp)>0);
    CHECK(GetTempFileNameW(temp,L"tlw",0,stage)>0); DeleteFileW(stage); CHECK(CreateDirectoryW(stage,NULL));
    path(folder,L"engines"); CHECK(CreateDirectoryW(folder,NULL));
    path(folder,L"engines\\modern"); CHECK(CreateDirectoryW(folder,NULL));
    ZeroMemory(&engine,sizeof(engine)); wcscpy(engine.base,stage); path(engine.exe,L"engines\\modern\\yt-dlp.exe");
    CHECK(CopyFileW(self,engine.exe,FALSE));
    CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_UNAVAILABLE);
    CHECK(out==NULL && engine_worker_closed());
    path(folder,L"plugins"); CHECK(CreateDirectoryW(folder,NULL));
    path(folder,L"plugins\\tubeliviano"); CHECK(CreateDirectoryW(folder,NULL));
    path(folder,L"plugins\\tubeliviano\\yt_dlp_plugins"); CHECK(CreateDirectoryW(folder,NULL));
    path(folder,L"plugins\\tubeliviano\\yt_dlp_plugins\\extractor"); CHECK(CreateDirectoryW(folder,NULL));
    path(folder,L"plugins\\tubeliviano\\yt_dlp_plugins\\extractor\\tubeliviano.py");
    { FILE *plugin=_wfopen(folder,L"wb"); CHECK(plugin!=NULL); if(plugin) { fputs("# native fixture only\n",plugin); fclose(plugin); } }
    path(logpath,L"calls.txt"); SetEnvironmentVariableW(L"TL_WORKER_LOG",logpath);
    SetEnvironmentVariableW(L"_PYI_ARCHIVE_FILE",L"must not reach engine");
    SetEnvironmentVariableW(L"_MEIPASS2",L"must not reach engine");
    SetEnvironmentVariableW(L"__COMPAT_LAYER",L"WINXPSP3");
    mode(L"normal"); resetlog();
    CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED);
    CHECK(out && strstr(out,"abcdefghijk")); CHECK(engine.failure==ENGINE_FAILURE_NONE); free_output(&out);
    CHECK(!engine_worker_closed());
    CHECK(count("BOOT",&pid)==1); CHECK(count("SEARCH",&search_pid)==1); CHECK(pid==search_pid);
    before=GetTickCount();
    CHECK(engine_worker_resolve(&engine,L"https://fixture.invalid/video",240,FALSE,NULL,2000,&out,error,768)==ENGINE_WORKER_HANDLED);
    CHECK(out && !strcmp(out,"https://cdn.invalid/video\n")); free_output(&out);
    CHECK(GetTickCount()-before<500); /* Reply must not wait for 700 ms cleanup. */
    CHECK(!engine_worker_closed());
    CHECK(count("BOOT",NULL)==1); CHECK(count("RESOLVE",&resolve_pid)==1); CHECK(pid==resolve_pid);
    CHECK(wait_closed(2000));
    CHECK(GetEnvironmentVariableW(L"_PYI_ARCHIVE_FILE",NULL,0)>0);
    SetEnvironmentVariableW(L"_PYI_ARCHIVE_FILE",NULL); SetEnvironmentVariableW(L"_MEIPASS2",NULL); SetEnvironmentVariableW(L"__COMPAT_LAYER",NULL);
    mode(L"split");
    CHECK(engine_worker_resolve(&engine,L"https://fixture.invalid/video",144,FALSE,NULL,2000,&out,error,768)==ENGINE_WORKER_HANDLED);
    CHECK(out && strstr(out,"video\nhttps://cdn.invalid/audio\n")); free_output(&out); CHECK(wait_closed(2000));
    mode(L"noisy"); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED); CHECK(out!=NULL); free_output(&out); stop();
    mode(L"error"); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED); CHECK(!out);
    CHECK(engine.failure==ENGINE_FAILURE_EXTRACT && wcsstr(error,L"Fallo extractor")); CHECK(wait_closed(2000));
    mode(L"loader"); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED); CHECK(!out);
    CHECK(engine.failure==ENGINE_FAILURE_LOADER && wcsstr(error,L"DLL")); CHECK(wait_closed(2000));
    for(i=0;i<2;++i) {
        mode(i ? L"nul" : L"bad"); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED);
        CHECK(!out && engine.failure==ENGINE_FAILURE_OUTPUT); CHECK(wait_closed(2000));
    }
    mode(L"boot-hang"); before=GetTickCount(); CHECK(search(NULL,200,&out,error)==ENGINE_WORKER_HANDLED);
    CHECK(!out && engine.failure==ENGINE_FAILURE_TIMEOUT); CHECK(GetTickCount()-before<1300); CHECK(wait_closed(2000));
    mode(L"partial"); CHECK(search(NULL,200,&out,error)==ENGINE_WORKER_HANDLED);
    CHECK(!out && engine.failure==ENGINE_FAILURE_TIMEOUT); CHECK(wait_closed(2000));
    cancellation(L"hang",FALSE,FALSE);
    cancellation(L"descendant",TRUE,FALSE);
    cancellation(L"no-read",FALSE,TRUE);
    mode(L"normal"); resetlog(); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED); CHECK(out!=NULL); free_output(&out);
    CHECK(wait_closed(2500)); /* Fixture build sets idle to 200 ms, not production 12 s. */
    CHECK(count("BOOT",NULL)==1);
    mode(L"normal"); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED); CHECK(out!=NULL); free_output(&out);
    engine_worker_stop();
    CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_HANDLED); CHECK(out!=NULL); free_output(&out); stop();
    {
        Async a; HANDLE thread; DWORD start;
        ZeroMemory(&a,sizeof(a)); a.cancel=CreateEventW(NULL,TRUE,FALSE,NULL);
        mode(L"hang"); resetlog(); thread=CreateThread(NULL,0,async_call,&a,0,NULL); CHECK(thread!=NULL);
        start=GetTickCount(); while(GetTickCount()-start<1000 && !count("SEARCH",NULL)) Sleep(10);
        CHECK(search(NULL,500,&out,error)==ENGINE_WORKER_HANDLED);
        CHECK(!out && engine.failure==ENGINE_FAILURE_IO);
        SetEvent(a.cancel); CHECK(WaitForSingleObject(thread,2000)==WAIT_OBJECT_0);
        CHECK(a.result.failure==ENGINE_FAILURE_CANCELLED); CloseHandle(thread); CloseHandle(a.cancel); CHECK(wait_closed(2000));
    }
    mode(L"unsupported"); resetlog(); CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_UNAVAILABLE); CHECK(!out); CHECK(wait_closed(2000));
    CHECK(count("BOOT",NULL)==1);
    CHECK(search(NULL,2000,&out,error)==ENGINE_WORKER_UNAVAILABLE); CHECK(count("BOOT",NULL)==1);
    { Engine deep=engine; wmemset(deep.base,L'x',230); deep.base[230]=0;
      CHECK(engine_worker_search(&deep,L"test",6,NULL,1000,&out,error,768)==ENGINE_WORKER_UNAVAILABLE); }
    DeleteFileW(engine.exe); DeleteFileW(logpath);
    path(folder,L"plugins\\tubeliviano\\yt_dlp_plugins\\extractor\\tubeliviano.py"); DeleteFileW(folder);
    path(folder,L"plugins\\tubeliviano\\yt_dlp_plugins\\extractor"); RemoveDirectoryW(folder);
    path(folder,L"plugins\\tubeliviano\\yt_dlp_plugins"); RemoveDirectoryW(folder);
    path(folder,L"plugins\\tubeliviano"); RemoveDirectoryW(folder); path(folder,L"plugins"); RemoveDirectoryW(folder);
    path(folder,L"engines\\modern"); RemoveDirectoryW(folder); path(folder,L"engines"); RemoveDirectoryW(folder); RemoveDirectoryW(stage);
    SetEnvironmentVariableW(L"TL_WORKER_MODE",NULL); SetEnvironmentVariableW(L"TL_WORKER_LOG",NULL);
    printf("Engine worker: %d checks; %d failed\n",checks,failed); return failed ? 1 : 0;
}
