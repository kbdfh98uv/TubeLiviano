/* Offline black-box regressions for actual Win32 main.c with fake_vlc.c.
 * Build with MinGW at the same architecture as the app under test:
 *   gcc -shared -Os tests/fake_vlc.c -o build/fake_vlc.dll
 *   gcc -municode -Os tests/main_regression_test.c -o build/main_regression_test.exe
 * Run: main_regression_test.exe <absolute app.exe> <absolute fake_vlc.dll>
 * Synthetic counters test control flow only, never actual audio/video quality. */
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

#define CAP(a) (sizeof(a)/sizeof((a)[0]))
enum { QUERY=100,SEARCH,PASTE,RESULTS,QUALITY,MODE,BRIDGE,PLAY,PAUSE,CANCEL,RETRY,OFFLINE };
static int checks,failures;
static DWORD app_pid;
static HWND app;
static WCHAR root[MAX_PATH], marker[MAX_PATH];
#define CHECK(x) do {++checks;if(!(x)){++failures;printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)

static void path(WCHAR *out,const WCHAR *base,const WCHAR *suffix)
{
    if(_snwprintf(out,MAX_PATH,L"%ls\\%ls",base,suffix)<0){out[0]=0;CHECK(FALSE);}
}
static BOOL create_empty(const WCHAR *name)
{
    HANDLE file=CreateFileW(name,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    if(file==INVALID_HANDLE_VALUE)return FALSE;
    CloseHandle(file);return TRUE;
}
static void copy_stage(const WCHAR *base,const WCHAR *application,const WCHAR *library,const WCHAR *self)
{
    WCHAR name[MAX_PATH];
    CHECK(CreateDirectoryW(base,NULL));
    path(name,base,L"TubeLiviano.exe");CHECK(CopyFileW(application,name,TRUE));
    path(name,base,L"vlc");CHECK(CreateDirectoryW(name,NULL));
    path(name,base,L"vlc\\libvlc.dll");CHECK(CopyFileW(library,name,TRUE));
    path(name,base,L"vlc\\libvlccore.dll");CHECK(CopyFileW(library,name,TRUE));
    path(name,base,L"media");CHECK(CreateDirectoryW(name,NULL));
    path(name,base,L"media\\prueba-240.mp4");CHECK(create_empty(name));
    path(name,base,L"engines");CHECK(CreateDirectoryW(name,NULL));
    path(name,base,L"engines\\modern");CHECK(CreateDirectoryW(name,NULL));
    path(name,base,L"engines\\modern\\yt-dlp.exe");CHECK(CopyFileW(self,name,TRUE));
    path(name,base,L"engines\\xp");CHECK(CreateDirectoryW(name,NULL));
    path(name,base,L"engines\\xp\\yt-dlp.exe");CHECK(CopyFileW(self,name,TRUE));
}
static BOOL CALLBACK find_window(HWND window,LPARAM ignored)
{
    DWORD owner;WCHAR cls[64];(void)ignored;
    GetWindowThreadProcessId(window,&owner);GetClassNameW(window,cls,64);
    if(owner==app_pid && !wcscmp(cls,L"TubeLiviano05")){app=window;return FALSE;}
    return TRUE;
}
static BOOL await_controls(PROCESS_INFORMATION *process)
{
    DWORD start=GetTickCount();
    app=NULL;app_pid=process->dwProcessId;
    do {
        EnumWindows(find_window,0);
        if(app && GetDlgItem(app,OFFLINE))return TRUE;
        if(WaitForSingleObject(process->hProcess,0)==WAIT_OBJECT_0)return FALSE;
        Sleep(20);
    }while(GetTickCount()-start<6000);
    return FALSE;
}
static BOOL launch(const WCHAR *base,const WCHAR *arguments,PROCESS_INFORMATION *process)
{
    WCHAR exe[MAX_PATH],command[2048];STARTUPINFOW startup;
    path(exe,base,L"TubeLiviano.exe");
    if(_snwprintf(command,CAP(command),L"\"%ls\" %ls",exe,arguments)<0)return FALSE;
    ZeroMemory(&startup,sizeof(startup));startup.cb=sizeof(startup);ZeroMemory(process,sizeof(*process));
    return CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,base,&startup,process);
}
static BOOL wait_exit(PROCESS_INFORMATION *process,DWORD timeout,DWORD *code)
{
    BOOL done=WaitForSingleObject(process->hProcess,timeout)==WAIT_OBJECT_0;
    if(!done)TerminateProcess(process->hProcess,99);
    WaitForSingleObject(process->hProcess,2000);GetExitCodeProcess(process->hProcess,code);
    CloseHandle(process->hThread);CloseHandle(process->hProcess);return done;
}
static void cleanup_engine(void)
{
    DWORD child=0;FILE *file=_wfopen(marker,L"r");
    if(file){fscanf(file,"%lu",&child);fclose(file);}
    if(child){HANDLE process=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,child);if(process){TerminateProcess(process,98);WaitForSingleObject(process,2000);CloseHandle(process);}}
    DeleteFileW(marker);
}
static BOOL contains(const WCHAR *file,const char *text)
{
    char buffer[4096];size_t n;FILE *input=_wfopen(file,L"rb");
    if(!input)return FALSE;
    n=fread(buffer,1,sizeof(buffer)-1,input);buffer[n]=0;fclose(input);
    return strstr(buffer,text)!=NULL;
}
static void self_test(const WCHAR *base,BOOL late_error)
{
    WCHAR arguments[2048],report[MAX_PATH],media[MAX_PATH];PROCESS_INFORMATION process;DWORD code=0;
    path(media,base,L"media\\prueba-240.mp4");
    path(report,base,late_error ? L"late-error.json" : L"missing-directory\\report.json");
    DeleteFileW(report);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_ERROR_MS",late_error ? L"400" : L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_STOP_MS",L"0");
    _snwprintf(arguments,CAP(arguments),L"--self-test-media \"%ls\" --report \"%ls\" --test-ms 1100",media,report);
    CHECK(launch(base,arguments,&process));if(!process.hProcess)return;
    CHECK(wait_exit(&process,8000,&code));
    printf("CASE %s exit=%lu\n",late_error ? "late-player-error" : "unwritable-report",(unsigned long)code);
    CHECK(code!=0);
    if(late_error) {CHECK(contains(report,"\"passed\":false"));CHECK(contains(report,"\"decoded_video\":12"));}
    else CHECK(GetFileAttributesW(report)==INVALID_FILE_ATTRIBUTES);
}
static void close_enter_test(const WCHAR *base)
{
    PROCESS_INFORMATION process;DWORD start,code=0;BOOL ready=FALSE;
    SetEnvironmentVariableW(L"TL_FAKE_VLC_ERROR_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_STOP_MS",L"1500");
    DeleteFileW(marker);
    CHECK(launch(base,L"",&process));if(!process.hProcess)return;
    CHECK(await_controls(&process));
    if(!app){wait_exit(&process,100,&code);return;}
    SendMessageW(GetDlgItem(app,OFFLINE),BM_CLICK,0,0);
    start=GetTickCount();
    while(GetTickCount()-start<4000){if(IsWindowEnabled(GetDlgItem(app,PAUSE))){ready=TRUE;break;}Sleep(20);}
    CHECK(ready);
    SetWindowTextW(GetDlgItem(app,QUERY),L"hang");
    PostMessageW(app,WM_CLOSE,0,0);
    Sleep(100); /* The deterministic fake stop is still running for 1.5 s. */
    PostMessageW(GetDlgItem(app,QUERY),WM_KEYDOWN,VK_RETURN,0);
    start=GetTickCount();
    CHECK(wait_exit(&process,6000,&code));
    printf("CASE close-enter exit=%lu elapsed=%lu marker=%d\n",(unsigned long)code,
           (unsigned long)(GetTickCount()-start),GetFileAttributesW(marker)!=INVALID_FILE_ATTRIBUTES);
    CHECK(code==0);CHECK(GetFileAttributesW(marker)==INVALID_FILE_ATTRIBUTES);
    cleanup_engine();
}
static BOOL found_path_message;
static BOOL CALLBACK find_path_message(HWND child,LPARAM ignored)
{
    WCHAR caption[2048];(void)ignored;
    GetWindowTextW(child,caption,CAP(caption));CharLowerBuffW(caption,(DWORD)wcslen(caption));
    if(wcsstr(caption,L"ruta") && !wcsstr(caption,L"falta el clip"))found_path_message=TRUE;
    return TRUE;
}
static void long_path_test(const WCHAR *application)
{
    WCHAR base[MAX_PATH],exe[MAX_PATH],suffix[MAX_PATH];size_t i,n;PROCESS_INFORMATION process;DWORD code=0;DWORD_PTR ignored;
    wcscpy(base,root);wcscat(base,L"\\");n=wcslen(base);
    for(i=n;i<242;i++)base[i]=L'p';
    base[242]=0;
    CHECK(CreateDirectoryW(base,NULL));
    path(exe,base,L"TubeLiviano.exe");CHECK(CopyFileW(application,exe,TRUE));
    CHECK(launch(base,L"",&process));if(!process.hProcess)return;
    CHECK(await_controls(&process));
    if(app) {
        CHECK(SendMessageTimeoutW(GetDlgItem(app,OFFLINE),BM_CLICK,0,0,SMTO_ABORTIFHUNG,2000,&ignored)!=0);
        found_path_message=FALSE;EnumChildWindows(app,find_path_message,0);
        printf("CASE long-path base-length=%u friendly-path-error=%d\n",(unsigned)wcslen(base),found_path_message);
        CHECK(found_path_message);
        PostMessageW(app,WM_CLOSE,0,0);
    }
    CHECK(wait_exit(&process,4000,&code));
    path(suffix,base,L"selftest.json");DeleteFileW(suffix);
    path(suffix,base,L"TubeLiviano.ini");DeleteFileW(suffix);
    DeleteFileW(exe);RemoveDirectoryW(base);
}
static void cleanup_stage(const WCHAR *base)
{
    const WCHAR *files[]={L"TubeLiviano.exe",L"vlc\\libvlc.dll",L"vlc\\libvlccore.dll",L"media\\prueba-240.mp4",
        L"engines\\modern\\yt-dlp.exe",L"engines\\xp\\yt-dlp.exe",L"late-error.json",L"TubeLiviano.ini"};
    const WCHAR *dirs[]={L"vlc",L"media",L"engines\\modern",L"engines\\xp",L"engines"};
    WCHAR name[MAX_PATH];unsigned i;
    for(i=0;i<CAP(files);i++){path(name,base,files[i]);DeleteFileW(name);}
    for(i=0;i<CAP(dirs);i++){path(name,base,dirs[i]);RemoveDirectoryW(name);}
    RemoveDirectoryW(base);
}
int wmain(int argc,WCHAR **argv)
{
    WCHAR temp[MAX_PATH],base[MAX_PATH],self[MAX_PATH];unsigned i;
    for(i=1;i<(unsigned)argc;i++)if(!wcscmp(argv[i],L"--dump-json")) {
        FILE *file;GetEnvironmentVariableW(L"TL_FAKE_ENGINE_MARKER",marker,MAX_PATH);
        file=_wfopen(marker,L"w");if(file){fprintf(file,"%lu",(unsigned long)GetCurrentProcessId());fclose(file);}
        Sleep(30000);return 1;
    }
    if(argc!=3){puts("usage: main_regression_test.exe APP.exe fake_vlc.dll");return 2;}
    GetTempPathW(MAX_PATH,temp);GetModuleFileNameW(NULL,self,MAX_PATH);
    /* Wine process IDs can repeat across fresh sessions while TEMP persists. */
    CHECK(GetTempFileNameW(temp,L"tlm",0,root)!=0);
    CHECK(DeleteFileW(root));
    CHECK(CreateDirectoryW(root,NULL));path(base,root,L"stage");
    path(marker,root,L"engine-started.txt");SetEnvironmentVariableW(L"TL_FAKE_ENGINE_MARKER",marker);
    copy_stage(base,argv[1],argv[2],self);
    self_test(base,TRUE);self_test(base,FALSE);close_enter_test(base);
    cleanup_stage(base);long_path_test(argv[1]);cleanup_engine();RemoveDirectoryW(root);
    SetEnvironmentVariableW(L"TL_FAKE_ENGINE_MARKER",NULL);
    printf("Main regressions: %d checks; %d failures (synthetic LibVLC, no playback claim)\n",checks,failures);
    return failures ? 1 : 0;
}
