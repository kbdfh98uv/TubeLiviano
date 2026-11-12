/* Black-box native UI/settings checks. No network or playback is started.
 * Build at the same architecture as the application under test:
 *   i686-w64-mingw32-gcc -municode -std=c99 -Os -Wall -Wextra -Werror \
 *       tests/ui_settings_test.c -lgdi32 -o build/ui_settings_test.exe
 * Run: ui_settings_test.exe <absolute application.exe> [screenshot directory]
 * The application and its INI are isolated in a fresh temporary directory. */
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
#include "../src/ui.h"
#include "../src/settings.h"

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
enum { GENERAL=500, PLAYBACK_PAGE=501, NETWORK_PAGE=502, DARK=506,
       LARGE=507, SETTINGS_QUALITY=510, SETTINGS_CACHE=511,
       SETTINGS_PREFETCH=512, SETTINGS_THREADS=513, SETTINGS_MODE=516,
       PRESET_SELECT=520, PRESET_PREPARE=521 };
static const WCHAR sample_query[]=L"Música para una PC liviana";
static const WCHAR bridge_session_url[]=L"http://192.168.1.70:8787?token=SESSION_ONLY";
static const WCHAR bridge_close_url[]=L"http://192.168.1.71:8787?token=CLOSE_ONLY";
static const WCHAR bridge_saved_url[]=L"http://192.168.1.71:8787";
static HWND app, dialog;
static DWORD app_pid;
static int checks, failures;
static const WCHAR *screenshot_directory;
#define CHECK(c) do { ++checks; if(!(c)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#c); } } while(0)

static void path(WCHAR *out, const WCHAR *base, const WCHAR *name) {
    int n=_snwprintf(out,MAX_PATH,L"%ls\\%ls",base,name);
    if(n<0 || n>=MAX_PATH) { out[0]=0; CHECK(FALSE); }
}
static void stage_engines(const WCHAR *application, const WCHAR *base, BOOL remove_files) {
    const WCHAR *engines[]={L"engines\\modern\\yt-dlp.exe",L"engines\\xp\\yt-dlp.exe"};
    const WCHAR *folders[]={L"engines",L"engines\\modern",L"engines\\xp"};
    WCHAR source_base[MAX_PATH],source[MAX_PATH],destination[MAX_PATH],*slash; unsigned i;
    if(remove_files) {
        for(i=0;i<COUNT(engines);++i) { path(destination,base,engines[i]); DeleteFileW(destination); }
        for(i=COUNT(folders);i>0;--i) { path(destination,base,folders[i-1]); RemoveDirectoryW(destination); }
        return;
    }
    if(!GetFullPathNameW(application,MAX_PATH,source_base,NULL))return;
    slash=wcsrchr(source_base,L'\\'); if(!slash)return; *slash=0;
    for(i=0;i<COUNT(engines);++i) {
        unsigned j; path(source,source_base,engines[i]);
        if(GetFileAttributesW(source)==INVALID_FILE_ATTRIBUTES)continue;
        for(j=0;j<COUNT(folders);++j) { path(destination,base,folders[j]); CreateDirectoryW(destination,NULL); }
        path(destination,base,engines[i]); CHECK(CopyFileW(source,destination,TRUE));
    }
}
static void screenshot(HWND window, const WCHAR *name) {
    WCHAR file[MAX_PATH]; RECT r; HDC dc,memory; HBITMAP bitmap; HGDIOBJ old;
    BITMAPINFO info; BITMAPFILEHEADER header; HANDLE output; BYTE *pixels;
    DWORD size,written; int width,height; BOOL ok;
    if(!screenshot_directory)return;
    SetForegroundWindow(window);
    RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    Sleep(100); GetWindowRect(window,&r); width=r.right-r.left; height=r.bottom-r.top;
    dc=GetWindowDC(window); memory=CreateCompatibleDC(dc);
    bitmap=CreateCompatibleBitmap(dc,width,height); old=SelectObject(memory,bitmap);
    ok=BitBlt(memory,0,0,width,height,dc,0,0,SRCCOPY);
    SelectObject(memory,old);
    ZeroMemory(&info,sizeof(info)); info.bmiHeader.biSize=sizeof(info.bmiHeader);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=24; info.bmiHeader.biCompression=BI_RGB;
    size=(DWORD)(((width*24+31)/32)*4*height);
    pixels=HeapAlloc(GetProcessHeap(),0,size);
    if(!pixels)ok=FALSE;
    if(ok)ok=GetDIBits(memory,bitmap,0,(UINT)height,pixels,&info,DIB_RGB_COLORS)!=0;
    ZeroMemory(&header,sizeof(header)); header.bfType=0x4d42;
    header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader); header.bfSize=header.bfOffBits+size;
    path(file,screenshot_directory,name);
    output=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(output==INVALID_HANDLE_VALUE)ok=FALSE;
    if(ok)ok=WriteFile(output,&header,sizeof(header),&written,NULL) && written==sizeof(header);
    if(ok)ok=WriteFile(output,&info.bmiHeader,sizeof(info.bmiHeader),&written,NULL) && written==sizeof(info.bmiHeader);
    if(ok)ok=WriteFile(output,pixels,size,&written,NULL) && written==size;
    if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    if(pixels)HeapFree(GetProcessHeap(),0,pixels);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(window,dc); CHECK(ok);
}
static BOOL CALLBACK find_window(HWND window, LPARAM settings) {
    DWORD pid; WCHAR name[80];
    GetWindowThreadProcessId(window,&pid);
    GetClassNameW(window,name,COUNT(name));
    if(pid==app_pid && !lstrcmpW(name,settings ? L"TubeLiviano.Settings" : L"TubeLiviano05")) {
        if(settings) dialog=window; else app=window;
        return FALSE;
    }
    return TRUE;
}
static BOOL launch(const WCHAR *exe, const WCHAR *base, PROCESS_INFORMATION *process) {
    WCHAR command[MAX_PATH+4]; STARTUPINFOW startup;
    _snwprintf(command,COUNT(command),L"\"%ls\"",exe);
    ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
    ZeroMemory(process,sizeof(*process));
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,base,&startup,process))return FALSE;
    app_pid=process->dwProcessId; app=NULL;
    return TRUE;
}
static BOOL await_app(const PROCESS_INFORMATION *process) {
    DWORD start=GetTickCount();
    do {
        EnumWindows(find_window,FALSE);
        if(app && GetDlgItem(app,RESULTS_NAV) && IsWindowVisible(GetDlgItem(app,SEARCH)) &&
           IsWindowVisible(GetDlgItem(app,PASTE)) && !IsWindowVisible(GetDlgItem(app,PLAY)) &&
           !IsWindowVisible(GetDlgItem(app,RESULTS)))return TRUE;
        if(WaitForSingleObject(process->hProcess,0)==WAIT_OBJECT_0)return FALSE;
        Sleep(20);
    } while(GetTickCount()-start<7000);
    return FALSE;
}
static BOOL close_app(PROCESS_INFORMATION *process) {
    BOOL ended;
    if(app && IsWindow(app))PostMessageW(app,WM_CLOSE,0,0);
    ended=WaitForSingleObject(process->hProcess,7000)==WAIT_OBJECT_0;
    if(!ended) { TerminateProcess(process->hProcess,99); WaitForSingleObject(process->hProcess,2000); }
    CloseHandle(process->hThread); CloseHandle(process->hProcess);
    ZeroMemory(process,sizeof(*process)); app=NULL;
    return ended;
}
static void click(HWND window, int id) {
    SendMessageW(GetDlgItem(window,id),BM_CLICK,0,0);
}
static BOOL wait_visible(HWND control, BOOL visible) {
    DWORD start=GetTickCount();
    do {
        if(IsWindowVisible(control)==visible)return TRUE;
        Sleep(20);
    } while(GetTickCount()-start<2000);
    return FALSE;
}
static HWND open_settings(void) {
    DWORD start=GetTickCount(); dialog=NULL;
    /* A synchronous BM_CLICK would block the driver inside the modal loop. */
    PostMessageW(app,WM_COMMAND,MAKEWPARAM(SETTINGS,BN_CLICKED),(LPARAM)GetDlgItem(app,SETTINGS));
    do {
        EnumWindows(find_window,TRUE);
        if(dialog && GetDlgItem(dialog,IDOK) && IsWindowVisible(GetDlgItem(dialog,DARK)))return dialog;
        Sleep(20);
    } while(GetTickCount()-start<4000);
    return NULL;
}
static BOOL close_settings(int action) {
    HWND old=dialog; DWORD start=GetTickCount();
    PostMessageW(dialog,WM_COMMAND,MAKEWPARAM(action,BN_CLICKED),(LPARAM)GetDlgItem(dialog,action));
    do {
        if(!IsWindow(old) && IsWindowEnabled(app)) { dialog=NULL; return TRUE; }
        Sleep(20);
    } while(GetTickCount()-start<4000);
    return FALSE;
}
static void select_combo(HWND window, int id, int index) {
    HWND combo=GetDlgItem(window,id);
    SendMessageW(combo,CB_SETCURSEL,(WPARAM)index,0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),(LPARAM)combo);
}
static BOOL query_is_preserved(HWND original) {
    WCHAR query[128];
    SendMessageW(GetDlgItem(app,QUERY),WM_GETTEXT,COUNT(query),(LPARAM)query);
    return GetDlgItem(app,QUERY)==original && !lstrcmpW(query,sample_query);
}
static BOOL edit_equals(HWND window, int id, const WCHAR *expected) {
    WCHAR value[256];
    SendMessageW(GetDlgItem(window,id),WM_GETTEXT,COUNT(value),(LPARAM)value);
    return !lstrcmpW(value,expected);
}
static void check_preset_drafts(void) {
    static const int quality_indices[PRESET_COUNT]={0,1,1,2,1};
    static const WCHAR *cache_values[PRESET_COUNT]={L"250",L"450",L"1500",L"600",L"800"};
    static const int prefetch_indices[PRESET_COUNT]={0,1,2,1,1};
    int preset;
    CHECK(SendMessageW(GetDlgItem(dialog,PRESET_SELECT),CB_GETCOUNT,0,0)==PRESET_COUNT);
    for(preset=0;preset<PRESET_COUNT;++preset) {
        select_combo(dialog,PRESET_SELECT,preset);
        click(dialog,PRESET_PREPARE);
        click(dialog,PLAYBACK_PAGE);
        CHECK(wait_visible(GetDlgItem(dialog,SETTINGS_QUALITY),TRUE));
        CHECK(SendMessageW(GetDlgItem(dialog,SETTINGS_QUALITY),CB_GETCURSEL,0,0)==quality_indices[preset]);
        CHECK(edit_equals(dialog,SETTINGS_CACHE,cache_values[preset]));
        CHECK(SendMessageW(GetDlgItem(dialog,SETTINGS_PREFETCH),CB_GETCURSEL,0,0)==prefetch_indices[preset]);
        CHECK(SendMessageW(GetDlgItem(dialog,SETTINGS_THREADS),CB_GETCURSEL,0,0)==(preset==PRESET_SMOOTH_360 ? 1 : 0));
        click(dialog,NETWORK_PAGE);
        CHECK(SendMessageW(GetDlgItem(dialog,SETTINGS_MODE),CB_GETCURSEL,0,0)==(preset==PRESET_BRIDGE ? 1 : 0));
        /* Preparing a draft never changes the underlying app or its mode. */
        CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCURSEL,0,0)==1);
        CHECK(SendMessageW(GetDlgItem(app,MODE),CB_GETCURSEL,0,0)==0);
        click(dialog,GENERAL);
    }
}
static BOOL background_is(COLORREF expected) {
    DWORD start=GetTickCount(); RECT r;
    do {
        HDC dc; COLORREF color;
        GetClientRect(app,&r); dc=GetDC(app);
        color=GetPixel(dc,4,r.bottom/2); ReleaseDC(app,dc);
        if(color==expected)return TRUE;
        Sleep(20);
    } while(GetTickCount()-start<2000);
    return FALSE;
}
static HWND video;
static BOOL CALLBACK find_video(HWND child, LPARAM unused) {
    WCHAR name[24]; (void)unused;
    GetClassNameW(child,name,COUNT(name));
    if(GetParent(child)==app && !lstrcmpiW(name,L"STATIC") &&
       (GetWindowLongPtrW(child,GWL_STYLE)&SS_TYPEMASK)==SS_BLACKRECT) { video=child; return FALSE; }
    return TRUE;
}
static void result_panel_alignment(void) {
    RECT left,right; video=NULL; EnumChildWindows(app,find_video,0);
    CHECK(video!=NULL);
    if(!video)return;
    GetWindowRect(GetDlgItem(app,RESULTS),&left); GetWindowRect(video,&right);
    CHECK(left.top==right.top); CHECK(left.bottom==right.bottom);
}
static void escape_checks(HWND query) {
    HWND combo=GetDlgItem(app,QUALITY); DWORD start;
    PostMessageW(query,WM_KEYDOWN,VK_ESCAPE,1); Sleep(80);
    CHECK(query_is_preserved(query));
    SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);
    CHECK(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0));
    PostMessageW(combo,WM_KEYDOWN,VK_ESCAPE,1); start=GetTickCount();
    while(SendMessageW(combo,CB_GETDROPPEDSTATE,0,0) && GetTickCount()-start<2000)Sleep(20);
    CHECK(!SendMessageW(combo,CB_GETDROPPEDSTATE,0,0));
    CHECK(query_is_preserved(query));
}
static BOOL CALLBACK control_is_inside(HWND control, LPARAM unused) {
    RECT r,client; (void)unused;
    if(GetParent(control)!=app || !IsWindowVisible(control))return TRUE;
    GetWindowRect(control,&r); MapWindowPoints(NULL,app,(POINT *)&r,2);
    GetClientRect(app,&client);
    if(r.left<0 || r.top<0 || r.right>client.right || r.bottom>client.bottom) {
        printf("Control %d outside %ldx%ld: %ld,%ld..%ld,%ld\n",GetDlgCtrlID(control),
            (long)client.right,(long)client.bottom,(long)r.left,(long)r.top,(long)r.right,(long)r.bottom);
        CHECK(FALSE);
    }
    return TRUE;
}
static void minimum_layout(void) {
    RECT r={0,0,660,450};
    AdjustWindowRectEx(&r,(DWORD)GetWindowLongPtrW(app,GWL_STYLE),FALSE,
                       (DWORD)GetWindowLongPtrW(app,GWL_EXSTYLE));
    CHECK(SetWindowPos(app,NULL,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER));
    UpdateWindow(app);
    EnumChildWindows(app,control_is_inside,0);
    select_combo(app,MODE,1);
    CHECK(wait_visible(GetDlgItem(app,BRIDGE),TRUE));
    EnumChildWindows(app,control_is_inside,0);
    screenshot(app,L"home-min-large-bridge.bmp");
    click(app,RESULTS_NAV);
    CHECK(wait_visible(GetDlgItem(app,PLAY),TRUE));
    EnumChildWindows(app,control_is_inside,0);
    result_panel_alignment();
    screenshot(app,L"results-min-large-bridge.bmp");
    select_combo(app,MODE,0);
    CHECK(wait_visible(GetDlgItem(app,BRIDGE),FALSE));
    click(app,HOME);
    CHECK(wait_visible(GetDlgItem(app,PASTE),TRUE));
}
static void check_pages(void) {
    CHECK(IsWindowVisible(GetDlgItem(dialog,DARK)));
    CHECK(!IsWindowVisible(GetDlgItem(dialog,SETTINGS_QUALITY)));
    screenshot(dialog,L"settings-general-dark.bmp");
    click(dialog,PLAYBACK_PAGE);
    CHECK(wait_visible(GetDlgItem(dialog,SETTINGS_QUALITY),TRUE));
    CHECK(!IsWindowVisible(GetDlgItem(dialog,DARK)));
    screenshot(dialog,L"settings-playback-dark.bmp");
    click(dialog,NETWORK_PAGE);
    CHECK(!IsWindowVisible(GetDlgItem(dialog,SETTINGS_QUALITY)));
    CHECK(IsWindowVisible(GetDlgItem(dialog,517)));
    screenshot(dialog,L"settings-network-dark.bmp");
    click(dialog,GENERAL);
    CHECK(wait_visible(GetDlgItem(dialog,DARK),TRUE));
}
static void dialog_resource_loop(HANDLE process) {
    DWORD before_gdi,before_user,after_gdi,after_user; int i;
    /* Warm class/font caches before comparing resources. */
    for(i=0;i<2;++i) { CHECK(open_settings()!=NULL); if(!dialog)return; CHECK(close_settings(IDCANCEL)); }
    before_gdi=GetGuiResources(process,GR_GDIOBJECTS);
    before_user=GetGuiResources(process,GR_USEROBJECTS);
    for(i=0;i<16;++i) { CHECK(open_settings()!=NULL); if(!dialog)return; CHECK(close_settings(IDCANCEL)); }
    after_gdi=GetGuiResources(process,GR_GDIOBJECTS);
    after_user=GetGuiResources(process,GR_USEROBJECTS);
    printf("Dialog resources: GDI %lu -> %lu, USER %lu -> %lu\n",(unsigned long)before_gdi,
        (unsigned long)after_gdi,(unsigned long)before_user,(unsigned long)after_user);
    if(before_gdi && after_gdi)CHECK(after_gdi<=before_gdi+2);
    else puts("NO EJECUTADO: GDI resource comparison unavailable in this Windows compatibility runtime.");
    if(before_user && after_user)CHECK(after_user<=before_user+3);
    else puts("NO EJECUTADO: USER resource comparison unavailable in this Windows compatibility runtime.");
}
int wmain(int argc, WCHAR **argv) {
    WCHAR temp[MAX_PATH],base[MAX_PATH],exe[MAX_PATH],ini[MAX_PATH];
    PROCESS_INFORMATION process; HWND original_query; int original_ini; WCHAR saved_bridge[256];
    if(argc!=2 && argc!=3) { puts("usage: ui_settings_test.exe APP.exe [SCREENSHOT_DIR]"); return 2; }
    if(argc==3) { screenshot_directory=argv[2]; CreateDirectoryW(screenshot_directory,NULL); }
    ZeroMemory(&process,sizeof(process));
    GetTempPathW(MAX_PATH,temp);
    CHECK(GetTempFileNameW(temp,L"tlu",0,base)!=0);
    CHECK(DeleteFileW(base));
    CHECK(CreateDirectoryW(base,NULL));
    path(exe,base,L"TubeLiviano.exe"); path(ini,base,L"TubeLiviano.ini");
    CHECK(CopyFileW(argv[1],exe,TRUE));
    stage_engines(argv[1],base,FALSE);
    CHECK(launch(exe,base,&process));
    if(!process.hProcess)goto clean;
    CHECK(await_app(&process));
    if(!app)goto finish;
    CHECK(IsWindowVisible(GetDlgItem(app,SEARCH)));
    CHECK(IsWindowVisible(GetDlgItem(app,PASTE)));
    CHECK(IsWindowVisible(GetDlgItem(app,OFFLINE)));
    CHECK(!IsWindowVisible(GetDlgItem(app,RESULTS)));
    CHECK(!IsWindowVisible(GetDlgItem(app,PLAY)));
    CHECK(!IsWindowVisible(GetDlgItem(app,BRIDGE)));
    CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCURSEL,0,0)==1);
    CHECK(SendMessageW(GetDlgItem(app,RESULTS),LB_GETITEMHEIGHT,0,0)==56);
    CHECK(GetWindowLongPtrW(GetDlgItem(app,RESULTS),GWL_STYLE)&LBS_NOINTEGRALHEIGHT);
    CHECK(background_is(RGB(16,16,16)));
    screenshot(app,L"home-dark.bmp");
    original_query=GetDlgItem(app,QUERY); SetWindowTextW(original_query,sample_query);
    escape_checks(original_query);
    click(app,RESULTS_NAV);
    CHECK(wait_visible(GetDlgItem(app,PLAY),TRUE));
    CHECK(!IsWindowVisible(GetDlgItem(app,PASTE)));
    CHECK(query_is_preserved(original_query));
    CHECK(!IsWindowVisible(GetDlgItem(app,RESULTS))); /* Empty-state help is painted by the app. */
    result_panel_alignment();
    screenshot(app,L"results-empty-dark.bmp");
    click(app,HOME); CHECK(wait_visible(GetDlgItem(app,PASTE),TRUE));
    CHECK(query_is_preserved(original_query));
    original_ini=(int)GetPrivateProfileIntW(L"General",L"DarkTheme",99,ini);
    CHECK(open_settings()!=NULL);
    if(!dialog)goto finish;
    CHECK(!IsWindowEnabled(app));
    CHECK(SendMessageW(GetDlgItem(dialog,DARK),BM_GETCHECK,0,0)==BST_CHECKED);
    CHECK(SendMessageW(GetDlgItem(dialog,LARGE),BM_GETCHECK,0,0)==BST_UNCHECKED);
    check_preset_drafts();
    check_pages(); click(dialog,DARK); click(dialog,LARGE);
    click(dialog,PLAYBACK_PAGE); select_combo(dialog,SETTINGS_QUALITY,0);
    CHECK(close_settings(IDCANCEL));
    CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCURSEL,0,0)==1);
    CHECK(SendMessageW(GetDlgItem(app,MODE),CB_GETCURSEL,0,0)==0);
    CHECK(SendMessageW(GetDlgItem(app,RESULTS),LB_GETITEMHEIGHT,0,0)==56);
    CHECK(background_is(RGB(16,16,16)));
    CHECK(query_is_preserved(original_query));
    CHECK((int)GetPrivateProfileIntW(L"General",L"DarkTheme",99,ini)==original_ini);
    CHECK(open_settings()!=NULL);
    if(!dialog)goto finish;
    click(dialog,DARK); click(dialog,LARGE);
    select_combo(dialog,PRESET_SELECT,PRESET_MINIMUM); click(dialog,PRESET_PREPARE);
    click(dialog,PLAYBACK_PAGE);
    CHECK(SendMessageW(GetDlgItem(dialog,SETTINGS_QUALITY),CB_GETCURSEL,0,0)==0);
    CHECK(edit_equals(dialog,SETTINGS_CACHE,L"250"));
    CHECK(close_settings(IDOK));
    CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCURSEL,0,0)==0);
    CHECK(SendMessageW(GetDlgItem(app,RESULTS),LB_GETITEMHEIGHT,0,0)==68);
    CHECK(background_is(RGB(248,248,248)));
    CHECK(query_is_preserved(original_query));
    screenshot(app,L"home-light-large.bmp");
    CHECK(GetPrivateProfileIntW(L"General",L"DarkTheme",99,ini)==0);
    CHECK(GetPrivateProfileIntW(L"General",L"LargeText",99,ini)==1);
    CHECK(GetPrivateProfileIntW(L"Playback",L"Quality",99,ini)==144);
    CHECK(GetPrivateProfileIntW(L"Playback",L"CacheMs",99,ini)==250);
    CHECK(GetPrivateProfileIntW(L"Playback",L"PrefetchKiB",99,ini)==64);
    CHECK(GetPrivateProfileIntW(L"Playback",L"DecoderThreads",99,ini)==1);
    minimum_layout(); dialog_resource_loop(process.hProcess);
    /* Neither operation sends EN_KILLFOCUS: keyboard shortcuts and WM_CLOSE
     * must read the current native bridge edit before copying/saving prefs. */
    select_combo(app,MODE,1);
    CHECK(wait_visible(GetDlgItem(app,BRIDGE),TRUE));
    CHECK(SetWindowTextW(GetDlgItem(app,BRIDGE),bridge_session_url));
    CHECK(open_settings()!=NULL);
    if(dialog) {
        click(dialog,NETWORK_PAGE);
        CHECK(edit_equals(dialog,517,bridge_session_url));
        CHECK(close_settings(IDCANCEL));
    }
    CHECK(edit_equals(app,BRIDGE,bridge_session_url));
    CHECK(SetWindowTextW(GetDlgItem(app,BRIDGE),bridge_close_url));
    CHECK(close_app(&process));
    GetPrivateProfileStringW(L"Network",L"BridgeURL",L"",saved_bridge,COUNT(saved_bridge),ini);
    CHECK(!lstrcmpW(saved_bridge,bridge_saved_url));
    CHECK(wcsstr(saved_bridge,L"token=")==NULL);
    CHECK(launch(exe,base,&process));
    if(!process.hProcess)goto clean;
    CHECK(await_app(&process));
    if(!app)goto finish;
    CHECK(SendMessageW(GetDlgItem(app,QUALITY),CB_GETCURSEL,0,0)==0);
    CHECK(SendMessageW(GetDlgItem(app,RESULTS),LB_GETITEMHEIGHT,0,0)==68);
    CHECK(background_is(RGB(248,248,248)));
    CHECK(SendMessageW(GetDlgItem(app,MODE),CB_GETCURSEL,0,0)==1);
    CHECK(IsWindowVisible(GetDlgItem(app,BRIDGE)));
    CHECK(edit_equals(app,BRIDGE,bridge_saved_url));
    CHECK(open_settings()!=NULL);
    if(dialog) {
        CHECK(SendMessageW(GetDlgItem(dialog,DARK),BM_GETCHECK,0,0)==BST_UNCHECKED);
        CHECK(SendMessageW(GetDlgItem(dialog,LARGE),BM_GETCHECK,0,0)==BST_CHECKED);
        CHECK(close_settings(IDCANCEL));
    }
finish:
    if(dialog && IsWindow(dialog))close_settings(IDCANCEL);
    if(process.hProcess)CHECK(close_app(&process));
clean:
    stage_engines(argv[1],base,TRUE);
    DeleteFileW(ini); DeleteFileW(exe); RemoveDirectoryW(base);
    printf("UI/settings: %d checks, %d failures (native controls, no network/playback)\n",checks,failures);
    return failures ? 1 : 0;
}
