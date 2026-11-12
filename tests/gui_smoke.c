/* Real native control smoke test with an offline fake extractor.
 * Build validation-only at the same architecture as the app under test:
 *   x86_64-w64-mingw32-gcc -municode -Os tests/gui_smoke.c -o build/gui_smoke.exe
 * Run: gui_smoke.exe <absolute path to validation app>
 * Never install this fixture as a real yt-dlp engine. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

enum { QUERY=100, SEARCH, PASTE, RESULTS, QUALITY, MODE, BRIDGE, PLAY,
       PAUSE, CANCEL, RETRY, OFFLINE };
static int checks, failures;
static DWORD pid;
static HWND app;
#define CHECK(c) do { ++checks; if(!(c)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#c); } } while(0)

static int has(int argc,WCHAR **argv,const WCHAR *s) {
    int i; for(i=1;i<argc;i++)if(!wcscmp(argv[i],s))return i; return 0;
}
static void append(const WCHAR *line) {
    char utf8[8192];
    int len=WideCharToMultiByte(CP_UTF8,0,line,-1,utf8,sizeof(utf8),NULL,NULL);
    FILE *f=fopen("gui-calls.log","ab");
    if(f) { if(len>0)fwrite(utf8,1,len-1,f); fputc('\n',f); fclose(f); }
}
static int fake(int argc,WCHAR **argv) {
    const WCHAR *target=argv[argc-1];
    int i=has(argc,argv,L"--format");
    if(has(argc,argv,L"--hold")) { Sleep(30000); return 0; }
    append(target);
    if(i && i+1<argc)append(argv[i+1]);
    if(has(argc,argv,L"--dump-json")) {
        if(!wcscmp(target,L"ytsearch12:hang")) {
            WCHAR self[MAX_PATH],cmd[MAX_PATH+20]; STARTUPINFOW si; PROCESS_INFORMATION pi;
            FILE *f;
            GetModuleFileNameW(NULL,self,MAX_PATH);
            _snwprintf(cmd,MAX_PATH+20,L"\"%ls\" --hold",self);
            ZeroMemory(&si,sizeof(si)); si.cb=sizeof(si); ZeroMemory(&pi,sizeof(pi));
            if(!CreateProcessW(self,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi))return 5;
            f=fopen("gui-child.pid","wb"); if(f) { fprintf(f,"%lu",(unsigned long)pi.dwProcessId); fclose(f); }
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread); Sleep(30000); return 0;
        }
        puts("{\"id\":\"abcdefghijk\",\"title\":\"caf\\u00e9 \\ud83d\\ude80\",\"channel\":\"Canal de prueba\",\"duration\":96}");
        puts("{\"id\":\"0123456789_\",\"title\":\"Segundo video\",\"channel\":\"Otro canal\",\"duration\":180}");
        return 0;
    }
    fputs("ERROR: fallo offline intencional para comprobar Reintentar",stderr);
    return 1;
}
static BOOL CALLBACK find_window(HWND h,LPARAM unused) {
    DWORD owner; WCHAR name[80]; (void)unused;
    GetWindowThreadProcessId(h,&owner); GetClassNameW(h,name,80);
    if(owner==pid && !wcscmp(name,L"TubeLiviano05")) { app=h; return FALSE; }
    return TRUE;
}
static void click(int id) { SendMessageW(GetDlgItem(app,id),BM_CLICK,0,0); }
static BOOL wait_button(int id,BOOL enabled,DWORD ms) {
    DWORD start=GetTickCount();
    do { if(IsWindowEnabled(GetDlgItem(app,id))==enabled)return TRUE; Sleep(20); }
    while(GetTickCount()-start<ms);
    return FALSE;
}
static HANDLE wait_child(const WCHAR *base) {
    WCHAR path[MAX_PATH]; DWORD start=GetTickCount();
    _snwprintf(path,MAX_PATH,L"%ls\\gui-child.pid",base);
    do {
        unsigned long child=0; FILE *f=_wfopen(path,L"rb");
        if(f) { int n=fscanf(f,"%lu",&child); fclose(f); if(n==1 && child)return OpenProcess(SYNCHRONIZE,FALSE,(DWORD)child); }
        Sleep(20);
    }while(GetTickCount()-start<5000);
    return NULL;
}
static BOOL log_contains(const WCHAR *base,const char *needle) {
    WCHAR path[MAX_PATH]; char data[16384]; size_t n; FILE *f;
    _snwprintf(path,MAX_PATH,L"%ls\\gui-calls.log",base);
    f=_wfopen(path,L"rb"); if(!f)return FALSE;
    n=fread(data,1,sizeof(data)-1,f); fclose(f); data[n]=0;
    return strstr(data,needle)!=NULL;
}
int wmain(int argc,WCHAR **argv) {
    WCHAR self[MAX_PATH],temp[MAX_PATH],parent_root[MAX_PATH],base[MAX_PATH],file[MAX_PATH],exe[MAX_PATH],command[MAX_PATH+4],title[256];
    STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD start; HANDLE child=NULL;
    if(has(argc,argv,L"--dump-json") || has(argc,argv,L"--get-url") || has(argc,argv,L"--hold"))return fake(argc,argv);
    if(argc!=2) { puts("usage: gui_smoke.exe APP.exe"); return 2; }
    GetModuleFileNameW(NULL,self,MAX_PATH); GetTempPathW(MAX_PATH,temp);
    CHECK(GetTempFileNameW(temp,L"tlg",0,parent_root)!=0);
    CHECK(DeleteFileW(parent_root)); CHECK(CreateDirectoryW(parent_root,NULL));
    _snwprintf(base,MAX_PATH,L"%ls\\TubeLiviano GUI caf\u00e9",parent_root);
    CHECK(CreateDirectoryW(base,NULL));
    _snwprintf(file,MAX_PATH,L"%ls\\engines",base); CHECK(CreateDirectoryW(file,NULL));
    _snwprintf(file,MAX_PATH,L"%ls\\engines\\modern",base); CHECK(CreateDirectoryW(file,NULL));
    wcscat(file,L"\\yt-dlp.exe"); CHECK(CopyFileW(self,file,TRUE));
    _snwprintf(file,MAX_PATH,L"%ls\\engines\\xp",base); CHECK(CreateDirectoryW(file,NULL));
    wcscat(file,L"\\yt-dlp.exe"); CHECK(CopyFileW(self,file,TRUE));
    _snwprintf(exe,MAX_PATH,L"%ls\\TubeLiviano.exe",base); CHECK(CopyFileW(argv[1],exe,TRUE));
    _snwprintf(command,MAX_PATH+4,L"\"%ls\"",exe);
    ZeroMemory(&si,sizeof(si)); si.cb=sizeof(si); ZeroMemory(&pi,sizeof(pi));
    CHECK(CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,base,&si,&pi));
    if(!pi.hProcess)return 2;
    pid=pi.dwProcessId;
    start=GetTickCount();
    do { EnumWindows(find_window,0); if(app)break; Sleep(20); }while(GetTickCount()-start<8000);
    CHECK(app!=NULL); if(!app) { TerminateProcess(pi.hProcess,1); return 2; }
    /* EnumWindows can see the top-level HWND while WM_CREATE is still creating
     * children. Wait for the final control, not just the visible parent. */
    start=GetTickCount();
    while(!GetDlgItem(app,OFFLINE) && GetTickCount()-start<5000)Sleep(20);
    GetWindowTextW(app,title,256); CHECK(wcsstr(title,L"0.5.4")!=NULL);
    CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCOUNT,0,0)==3);
    CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCURSEL,0,0)==1);
    CHECK(SendMessageW(GetDlgItem(app,MODE),CB_GETCOUNT,0,0)==2);
    CHECK(!IsWindowEnabled(GetDlgItem(app,RETRY)));
    SetWindowTextW(GetDlgItem(app,QUERY),L"caf\u00e9"); click(SEARCH);
    CHECK(wait_button(RETRY,TRUE,8000));
    CHECK(SendMessageW(GetDlgItem(app,RESULTS),LB_GETCOUNT,0,0)==2);
    CHECK(SendMessageW(GetDlgItem(app,RESULTS),LB_GETITEMHEIGHT,0,0)>=45);
    SendMessageW(GetDlgItem(app,RESULTS),LB_GETTEXT,0,(LPARAM)title);
    CHECK(!wcscmp(title,L"caf\u00e9 \xd83d\xde80"));
    SetWindowTextW(GetDlgItem(app,QUERY),L""); click(PLAY);
    CHECK(wait_button(RETRY,TRUE,8000));
    CHECK(log_contains(base,"https://www.youtube.com/watch?v=abcdefghijk"));
    CHECK(log_contains(base,"[height<=240]"));
    SendMessageW(GetDlgItem(app,QUALITY),CB_SETCURSEL,0,0);
    SendMessageW(app,WM_COMMAND,MAKEWPARAM(QUALITY,CBN_SELCHANGE),(LPARAM)GetDlgItem(app,QUALITY));
    CHECK(wait_button(RETRY,TRUE,8000)); CHECK(log_contains(base,"[height<=144]"));
    click(RETRY); CHECK(wait_button(RETRY,TRUE,8000));
    CHECK(!IsWindowEnabled(GetDlgItem(app,PAUSE)));
    /* Cancel from the actual UI must terminate an extractor grandchild. */
    SetWindowTextW(GetDlgItem(app,QUERY),L"hang"); click(SEARCH);
    child=wait_child(base); CHECK(child!=NULL); click(CANCEL);
    CHECK(wait_button(CANCEL,FALSE,5000));
    if(child) { CHECK(WaitForSingleObject(child,2000)==WAIT_OBJECT_0); CloseHandle(child); child=NULL; }
    CHECK(IsWindowEnabled(GetDlgItem(app,RETRY)));
    _snwprintf(file,MAX_PATH,L"%ls\\gui-child.pid",base); DeleteFileW(file);
    click(RETRY); child=wait_child(base); CHECK(child!=NULL);
    PostMessageW(app,WM_CLOSE,0,0);
    CHECK(WaitForSingleObject(pi.hProcess,7000)==WAIT_OBJECT_0);
    if(child) { CHECK(WaitForSingleObject(child,2000)==WAIT_OBJECT_0); CloseHandle(child); }
    if(WaitForSingleObject(pi.hProcess,0)!=WAIT_OBJECT_0)TerminateProcess(pi.hProcess,1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    _snwprintf(file,MAX_PATH,L"%ls\\gui-child.pid",base); DeleteFileW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\gui-calls.log",base); DeleteFileW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\engines\\modern\\yt-dlp.exe",base); DeleteFileW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\engines\\xp\\yt-dlp.exe",base); DeleteFileW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\engines\\modern",base); RemoveDirectoryW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\engines\\xp",base); RemoveDirectoryW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\engines",base); RemoveDirectoryW(file);
    _snwprintf(file,MAX_PATH,L"%ls\\TubeLiviano.ini",base); DeleteFileW(file);
    DeleteFileW(exe); RemoveDirectoryW(base); RemoveDirectoryW(parent_root);
    printf("GUI: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
