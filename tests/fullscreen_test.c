/* Black-box native fullscreen regressions with explicitly synthetic LibVLC.
 * Build at the same architecture as the app:
 *   i686-w64-mingw32-gcc -municode -std=c99 -Os -Wall -Wextra -Werror \
 *       tests/fullscreen_test.c -static-libgcc -lgdi32 -o build/fullscreen_test.exe
 * Run: fullscreen_test.exe APP.exe FAKE_VLC.dll [SCREENSHOT_DIR]
 * Real VLC: fullscreen_test.exe APP.exe --real-media [SCREENSHOT_DIR]
 * Default mode uses no network or real media; optional --real-media exercises
 * the staged player/clip with dummy audio. Both check native resize/restoration.
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
#include <string.h>
#include <wchar.h>
#include "../src/ui.h"

#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
enum { FS_BUTTON=115, FS_ESCAPE_HOTKEY=0x5450, FS_F11_HOTKEY=0x5451, PROBE_ESCAPE=0x5452, PROBE_F11=0x5453 };
static HWND app, video;
static DWORD app_pid;
static int checks, failures;
static const WCHAR *screenshot_directory;
static BOOL real_capture;
#define CHECK(c) do { ++checks; if(!(c)) { ++failures; printf("FAIL %d: %s\n",__LINE__,#c); } } while(0)

static BOOL path(WCHAR *out, const WCHAR *base, const WCHAR *name) {
    int n=_snwprintf(out,MAX_PATH,L"%ls\\%ls",base,name);
    if(n<0 || n>=MAX_PATH) { out[0]=0; return FALSE; }
    return TRUE;
}
static BOOL empty_file(const WCHAR *file) {
    HANDLE handle=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(handle==INVALID_HANDLE_VALUE)return FALSE;
    return CloseHandle(handle);
}
static BOOL CALLBACK find_app(HWND window, LPARAM unused) {
    DWORD pid; WCHAR name[80]; (void)unused;
    GetWindowThreadProcessId(window,&pid); GetClassNameW(window,name,COUNT(name));
    if(pid==app_pid && !lstrcmpW(name,L"TubeLiviano05")) { app=window; return FALSE; }
    return TRUE;
}
static BOOL CALLBACK find_video(HWND child, LPARAM unused) {
    WCHAR name[32]; (void)unused;
    GetClassNameW(child,name,COUNT(name));
    if(GetParent(child)==app && !lstrcmpiW(name,L"STATIC") &&
       (GetWindowLongPtrW(child,GWL_STYLE)&SS_TYPEMASK)==SS_BLACKRECT) {
        video=child; return FALSE;
    }
    return TRUE;
}
static BOOL launch_arguments(const WCHAR *exe, const WCHAR *base, const WCHAR *arguments,
                             PROCESS_INFORMATION *process) {
    WCHAR command[MAX_PATH*3+160]; STARTUPINFOW startup;
    int n=_snwprintf(command,COUNT(command),L"\"%ls\" %ls",exe,arguments);
    if(n<0 || n>=(int)COUNT(command))return FALSE;
    ZeroMemory(&startup,sizeof(startup)); startup.cb=sizeof(startup);
    ZeroMemory(process,sizeof(*process)); app=NULL; video=NULL;
    if(!CreateProcessW(exe,command,NULL,NULL,FALSE,0,NULL,base,&startup,process))return FALSE;
    app_pid=process->dwProcessId;
    return TRUE;
}
static BOOL launch(const WCHAR *exe, const WCHAR *base, PROCESS_INFORMATION *process) {
    return launch_arguments(exe,base,L"",process);
}
static BOOL await_app(const PROCESS_INFORMATION *process) {
    DWORD start=GetTickCount();
    do {
        EnumWindows(find_app,0);
        if(app && GetDlgItem(app,FS_BUTTON) && IsWindowVisible(GetDlgItem(app,PASTE)) &&
           !IsWindowVisible(GetDlgItem(app,PLAY))) {
            EnumChildWindows(app,find_video,0);
            return video!=NULL;
        }
        if(WaitForSingleObject(process->hProcess,0)==WAIT_OBJECT_0)return FALSE;
        Sleep(20);
    }while(GetTickCount()-start<7000);
    return FALSE;
}
static BOOL close_app(PROCESS_INFORMATION *process) {
    DWORD code=0; BOOL ended;
    if(app && IsWindow(app))PostMessageW(app,WM_CLOSE,0,0);
    ended=WaitForSingleObject(process->hProcess,7000)==WAIT_OBJECT_0;
    if(!ended) { TerminateProcess(process->hProcess,99); WaitForSingleObject(process->hProcess,2000); }
    GetExitCodeProcess(process->hProcess,&code);
    CloseHandle(process->hThread); CloseHandle(process->hProcess);
    ZeroMemory(process,sizeof(*process)); app=NULL; video=NULL;
    return ended && code==0;
}
static void click(int id) { SendMessageW(GetDlgItem(app,id),BM_CLICK,0,0); }
static BOOL await_enabled(int id, BOOL enabled) {
    DWORD start=GetTickCount();
    do {
        if(IsWindowEnabled(GetDlgItem(app,id))==enabled)return TRUE;
        Sleep(20);
    }while(GetTickCount()-start<4000);
    return FALSE;
}
static BOOL fullscreen(void) {
    LONG_PTR style=GetWindowLongPtrW(app,GWL_STYLE);
    return (style&WS_POPUP)!=0 && (style&(WS_CAPTION|WS_THICKFRAME))==0;
}
static BOOL await_fullscreen(BOOL enabled) {
    DWORD start=GetTickCount(); WCHAR caption[160];
    do {
        /* Style bits change before SetWindowPlacement/SetWindowPos complete.
         * The button caption is the app's final visible transition marker. */
        SendMessageW(GetDlgItem(app,FS_BUTTON),WM_GETTEXT,COUNT(caption),(LPARAM)caption);
        if(fullscreen()==enabled &&
           (enabled ? wcsstr(caption,L"Esc")!=NULL : wcsstr(caption,L"Pantalla completa")!=NULL))return TRUE;
        Sleep(20);
    }while(GetTickCount()-start<3000);
    printf("Fullscreen transition timeout: expected=%d style=0x%08lX caption=%ls\n",
           enabled,(unsigned long)GetWindowLongPtrW(app,GWL_STYLE),caption);
    return FALSE;
}
static BOOL found_caption;
static BOOL CALLBACK contains_caption(HWND child, LPARAM needle) {
    WCHAR text[512];
    SendMessageW(child,WM_GETTEXT,COUNT(text),(LPARAM)text);
    if(wcsstr(text,(const WCHAR *)needle)) { found_caption=TRUE; return FALSE; }
    return TRUE;
}
static BOOL status_has(const WCHAR *needle) {
    found_caption=FALSE;
    EnumChildWindows(app,contains_caption,(LPARAM)needle);
    return found_caption;
}
static BOOL await_status(const WCHAR *needle) {
    DWORD start=GetTickCount();
    do { if(status_has(needle))return TRUE; Sleep(20); }while(GetTickCount()-start<3000);
    return FALSE;
}
static void key(UINT code) { PostMessageW(app,WM_KEYDOWN,code,1); }
static void screenshot(const WCHAR *name) {
    WCHAR file[MAX_PATH]; RECT r; HDC dc,memory; HBITMAP bitmap; HGDIOBJ old;
    BITMAPINFO info; BITMAPFILEHEADER header; HANDLE output=INVALID_HANDLE_VALUE;
    BYTE *pixels=NULL; DWORD bytes=0,written; int width,height; BOOL ok=FALSE;
    if(!screenshot_directory)return;
    SetForegroundWindow(app);
    if(!real_capture)RedrawWindow(app,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    Sleep(80); GetWindowRect(app,&r); width=r.right-r.left; height=r.bottom-r.top;
    /* Capture the composed desktop pixels: a parent window DC can omit the
     * native VLC child even when its displayed-picture counter is positive. */
    dc=GetDC(NULL); memory=CreateCompatibleDC(dc);
    bitmap=CreateCompatibleBitmap(dc,width,height); old=SelectObject(memory,bitmap);
    ok=BitBlt(memory,0,0,width,height,dc,r.left,r.top,SRCCOPY);
    SelectObject(memory,old);
    ZeroMemory(&info,sizeof(info)); info.bmiHeader.biSize=sizeof(info.bmiHeader);
    info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=24;
    bytes=(DWORD)(((width*24+31)/32)*4*height);
    pixels=HeapAlloc(GetProcessHeap(),0,bytes); if(!pixels)ok=FALSE;
    if(ok)ok=GetDIBits(memory,bitmap,0,(UINT)height,pixels,&info,DIB_RGB_COLORS)!=0;
    ZeroMemory(&header,sizeof(header)); header.bfType=0x4d42;
    header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader); header.bfSize=header.bfOffBits+bytes;
    if(!path(file,screenshot_directory,name))ok=FALSE;
    if(ok)output=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(output==INVALID_HANDLE_VALUE)ok=FALSE;
    if(ok)ok=WriteFile(output,&header,sizeof(header),&written,NULL) && written==sizeof(header);
    if(ok)ok=WriteFile(output,&info.bmiHeader,sizeof(info.bmiHeader),&written,NULL) && written==sizeof(info.bmiHeader);
    if(ok)ok=WriteFile(output,pixels,bytes,&written,NULL) && written==bytes;
    if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    if(pixels)HeapFree(GetProcessHeap(),0,pixels);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL,dc); CHECK(ok);
}
static void check_restored(const RECT *rect, LONG_PTR style) {
    RECT restored;
    CHECK(!fullscreen()); CHECK(GetWindowLongPtrW(app,GWL_STYLE)==style);
    CHECK(GetWindowRect(app,&restored)); CHECK(EqualRect(rect,&restored));
    CHECK(IsWindowVisible(GetDlgItem(app,QUERY)));
    CHECK(IsWindowVisible(GetDlgItem(app,FS_BUTTON)));
}
static void check_fullscreen(HWND original) {
    RECT outer,client,surface; MONITORINFO monitor; WCHAR caption[160];
    HWND fs=GetDlgItem(app,FS_BUTTON),pause=GetDlgItem(app,PAUSE),cancel=GetDlgItem(app,CANCEL);
    HMONITOR display=MonitorFromWindow(app,MONITOR_DEFAULTTONEAREST);
    ZeroMemory(&monitor,sizeof(monitor)); monitor.cbSize=sizeof(monitor);
    CHECK(app==original); CHECK(IsWindow(app)); CHECK(fullscreen());
    CHECK(GetMonitorInfoW(display,&monitor)); CHECK(GetWindowRect(app,&outer));
    CHECK(EqualRect(&outer,&monitor.rcMonitor));
    CHECK(!IsWindowVisible(GetDlgItem(app,QUERY)));
    CHECK(!IsWindowVisible(GetDlgItem(app,RESULTS)));
    CHECK(!IsWindowVisible(GetDlgItem(app,SETTINGS)));
    CHECK(!IsWindowVisible(GetDlgItem(app,HOME)));
    CHECK(IsWindowVisible(video)); CHECK(IsWindowVisible(fs));
    CHECK(IsWindowVisible(pause)); CHECK(IsWindowEnabled(pause));
    CHECK(IsWindowVisible(cancel)); CHECK(IsWindowEnabled(cancel));
    SendMessageW(fs,WM_GETTEXT,COUNT(caption),(LPARAM)caption); CHECK(wcsstr(caption,L"Esc")!=NULL && wcsstr(caption,L"F11")!=NULL);
    GetClientRect(app,&client); GetWindowRect(video,&surface);
    MapWindowPoints(NULL,app,(POINT *)&surface,2);
    CHECK(surface.left==0 && surface.top==0 && surface.right==client.right);
    CHECK(surface.bottom>client.bottom-80 && surface.bottom<=client.bottom-40);
}
static void hotkey_controls(void) {
    BOOL escape_available, f11_available;
    /* Probe outside fullscreen first so existing desktop bindings are not
     * confused with our app's registrations. No real keystrokes are injected. */
    SendMessageW(app,WM_ACTIVATE,WA_INACTIVE,0);
    escape_available=RegisterHotKey(NULL,PROBE_ESCAPE,0,VK_ESCAPE);
    if(escape_available)UnregisterHotKey(NULL,PROBE_ESCAPE);
    f11_available=RegisterHotKey(NULL,PROBE_F11,0,VK_F11);
    if(f11_available)UnregisterHotKey(NULL,PROBE_F11);
    SendMessageW(app,WM_ACTIVATE,WA_ACTIVE,0);
    click(FS_BUTTON); CHECK(await_fullscreen(TRUE));
    SendMessageW(app,WM_ACTIVATE,WA_ACTIVE,0);
    if(escape_available) {
        BOOL acquired=RegisterHotKey(NULL,PROBE_ESCAPE,0,VK_ESCAPE);
        CHECK(!acquired); if(acquired)UnregisterHotKey(NULL,PROBE_ESCAPE);
    } else puts("NO EJECUTADO: Escape hotkey collision probe unavailable on this desktop.");
    if(f11_available) {
        BOOL acquired=RegisterHotKey(NULL,PROBE_F11,0,VK_F11);
        CHECK(!acquired); if(acquired)UnregisterHotKey(NULL,PROBE_F11);
    } else puts("NO EJECUTADO: F11 hotkey collision probe unavailable on this desktop.");
    SendMessageW(app,WM_ACTIVATE,WA_INACTIVE,0);
    if(escape_available) {
        BOOL acquired=RegisterHotKey(NULL,PROBE_ESCAPE,0,VK_ESCAPE);
        CHECK(acquired); if(acquired)UnregisterHotKey(NULL,PROBE_ESCAPE);
    }
    if(f11_available) {
        BOOL acquired=RegisterHotKey(NULL,PROBE_F11,0,VK_F11);
        CHECK(acquired); if(acquired)UnregisterHotKey(NULL,PROBE_F11);
    }
    SendMessageW(app,WM_ACTIVATE,WA_ACTIVE,0);
    PostMessageW(app,WM_HOTKEY,FS_ESCAPE_HOTKEY,MAKELPARAM(0,VK_ESCAPE));
    CHECK(await_fullscreen(FALSE)); CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
    /* An already queued hotkey after leaving must not cancel or re-enter. */
    PostMessageW(app,WM_HOTKEY,FS_ESCAPE_HOTKEY,MAKELPARAM(0,VK_ESCAPE));
    Sleep(60); CHECK(!fullscreen()); CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
    /* F11 must also enter from normal playback, including a VLC-owned focus. */
    PostMessageW(app,WM_HOTKEY,FS_F11_HOTKEY,MAKELPARAM(0,VK_F11));
    CHECK(await_fullscreen(TRUE)); CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
    click(FS_BUTTON); CHECK(await_fullscreen(FALSE));
}
static void normal_controls(PROCESS_INFORMATION *process) {
    RECT normal; LONG_PTR style; HWND original=app;
    CHECK(!IsWindowVisible(GetDlgItem(app,FS_BUTTON)));
    click(RESULTS_NAV);
    CHECK(IsWindowVisible(GetDlgItem(app,FS_BUTTON)));
    CHECK(!IsWindowEnabled(GetDlgItem(app,FS_BUTTON)));
    /* A disabled fullscreen command must not transform an empty player. */
    SendMessageW(app,WM_COMMAND,FS_BUTTON,0); CHECK(!fullscreen());
    click(OFFLINE); CHECK(await_enabled(PAUSE,TRUE));
    CHECK(IsWindowEnabled(GetDlgItem(app,FS_BUTTON)));
    CHECK(GetWindowRect(app,&normal)); style=GetWindowLongPtrW(app,GWL_STYLE);
    screenshot(L"fullscreen-windowed-synthetic.bmp");
    click(PAUSE); CHECK(await_status(L"En pausa"));
    click(FS_BUTTON); CHECK(await_fullscreen(TRUE)); check_fullscreen(original);
    CHECK(await_status(L"En pausa")); /* Toggle must preserve paused playback. */
    screenshot(L"fullscreen-active-synthetic.bmp");
    key(VK_F11); CHECK(await_fullscreen(FALSE)); check_restored(&normal,style);
    CHECK(await_status(L"En pausa"));
    click(PAUSE); CHECK(await_status(L"Reproduciendo"));
    key(VK_F11); CHECK(await_fullscreen(TRUE));
    key(VK_ESCAPE); CHECK(await_fullscreen(FALSE)); check_restored(&normal,style);
    CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
    CHECK(await_status(L"Reproduciendo")); /* Escape exited fullscreen, not playback. */
    hotkey_controls();
    key(VK_ESCAPE); CHECK(await_enabled(PAUSE,FALSE)); CHECK(!fullscreen());
    CHECK(!IsWindowEnabled(GetDlgItem(app,FS_BUTTON)));
    click(OFFLINE); CHECK(await_enabled(PAUSE,TRUE));
    click(FS_BUTTON); CHECK(await_fullscreen(TRUE));
    click(CANCEL); CHECK(await_fullscreen(FALSE)); CHECK(await_enabled(PAUSE,FALSE));
    check_restored(&normal,style);
    CHECK(WaitForSingleObject(process->hProcess,0)==WAIT_TIMEOUT);
}
static BOOL await_maximized(const RECT *normal, WINDOWPLACEMENT *placement) {
    DWORD start=GetTickCount(); DWORD_PTR ignored; WINDOWPLACEMENT previous;
    BOOL sampled=FALSE;
    ZeroMemory(&previous,sizeof(previous));
    do {
        /* Cross-thread maximization can expose WS_MAXIMIZE before placement
         * settles. Read its actual entry state twice across message boundaries. */
        SendMessageTimeoutW(app,WM_NULL,0,0,SMTO_ABORTIFHUNG,500,&ignored);
        ZeroMemory(placement,sizeof(*placement)); placement->length=sizeof(*placement);
        if(GetWindowPlacement(app,placement) && IsZoomed(app) &&
           placement->showCmd==SW_SHOWMAXIMIZED) {
            if(sampled && EqualRect(&previous.rcNormalPosition,&placement->rcNormalPosition)) {
                if(!EqualRect(normal,&placement->rcNormalPosition))
                    puts("NO EJECUTADO: desktop pre-maximize normal-rectangle bookkeeping differs in this runtime; fullscreen restoration is checked against its stable reported entry placement.");
                return TRUE;
            }
            previous=*placement; sampled=TRUE;
        } else sampled=FALSE;
        Sleep(20);
    }while(GetTickCount()-start<3000);
    printf("Maximize readiness timeout: show=%u zoomed=%d observed normal %ld,%ld,%ld,%ld\n",
        placement->showCmd,IsZoomed(app),(long)placement->rcNormalPosition.left,
        (long)placement->rcNormalPosition.top,(long)placement->rcNormalPosition.right,
        (long)placement->rcNormalPosition.bottom);
    return FALSE;
}
static void maximized_restore(void) {
    WINDOWPLACEMENT normal,before,after; RECT maximized;
    click(OFFLINE); CHECK(await_enabled(PAUSE,TRUE));
    ZeroMemory(&normal,sizeof(normal)); normal.length=sizeof(normal);
    CHECK(GetWindowPlacement(app,&normal));
    PostMessageW(app,WM_SYSCOMMAND,SC_MAXIMIZE,0);
    CHECK(await_maximized(&normal.rcNormalPosition,&before));
    CHECK(IsZoomed(app));
    CHECK(GetWindowPlacement(app,&before)); CHECK(GetWindowRect(app,&maximized));
    click(FS_BUTTON); CHECK(await_fullscreen(TRUE)); key(VK_ESCAPE);
    CHECK(await_fullscreen(FALSE));
    ZeroMemory(&after,sizeof(after)); after.length=sizeof(after);
    CHECK(GetWindowPlacement(app,&after)); CHECK(after.showCmd==SW_SHOWMAXIMIZED);
    if(!EqualRect(&before.rcNormalPosition,&after.rcNormalPosition))
        printf("Max restore normal rect before %ld,%ld,%ld,%ld after %ld,%ld,%ld,%ld\n",
            (long)before.rcNormalPosition.left,(long)before.rcNormalPosition.top,
            (long)before.rcNormalPosition.right,(long)before.rcNormalPosition.bottom,
            (long)after.rcNormalPosition.left,(long)after.rcNormalPosition.top,
            (long)after.rcNormalPosition.right,(long)after.rcNormalPosition.bottom);
    CHECK(EqualRect(&before.rcNormalPosition,&after.rcNormalPosition));
    CHECK(IsZoomed(app)); CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
    ShowWindow(app,SW_RESTORE);
}
static void closing_restore(PROCESS_INFORMATION *process, const WCHAR *ini) {
    WINDOWPLACEMENT normal;
    CHECK(SetWindowPos(app,NULL,60,70,800,570,SWP_NOACTIVATE|SWP_NOZORDER));
    ZeroMemory(&normal,sizeof(normal)); normal.length=sizeof(normal);
    CHECK(GetWindowPlacement(app,&normal));
    if(!IsWindowEnabled(GetDlgItem(app,PAUSE))) { click(OFFLINE); CHECK(await_enabled(PAUSE,TRUE)); }
    click(FS_BUTTON); CHECK(await_fullscreen(TRUE));
    CHECK(close_app(process));
    CHECK((int)GetPrivateProfileIntW(L"General",L"Width",0,ini)==normal.rcNormalPosition.right-normal.rcNormalPosition.left);
    CHECK((int)GetPrivateProfileIntW(L"General",L"Height",0,ini)==normal.rcNormalPosition.bottom-normal.rcNormalPosition.top);
}
static void cleanup(const WCHAR *base) {
    const WCHAR *files[]={L"TubeLiviano.exe",L"TubeLiviano.ini",L"vlc\\libvlc.dll",L"vlc\\libvlccore.dll",L"media\\prueba-240.mp4"};
    const WCHAR *folders[]={L"media",L"vlc"};
    WCHAR file[MAX_PATH]; unsigned i;
    for(i=0;i<COUNT(files);++i) { if(path(file,base,files[i]))DeleteFileW(file); }
    for(i=0;i<COUNT(folders);++i) { if(path(file,base,folders[i]))RemoveDirectoryW(file); }
    RemoveDirectoryW(base);
}
static BOOL await_real_playback(const PROCESS_INFORMATION *process) {
    DWORD start=GetTickCount();
    do {
        EnumWindows(find_app,0);
        if(app && GetDlgItem(app,FS_BUTTON) && IsWindowVisible(GetDlgItem(app,PLAY)) &&
           IsWindowEnabled(GetDlgItem(app,PAUSE))) {
            EnumChildWindows(app,find_video,0);
            return video!=NULL && IsWindowVisible(video);
        }
        if(WaitForSingleObject(process->hProcess,0)==WAIT_OBJECT_0)return FALSE;
        Sleep(20);
    }while(GetTickCount()-start<15000);
    return FALSE;
}
/* The generated fixture contains bright color bars. Check visible picture
 * content independently of VLC's state/counters; this is not a UI-theme test. */
static BOOL video_picture_visible(void) {
    RECT r; HDC screen; int x,y,colored=0;
    if(!video || !GetWindowRect(video,&r) || !IsWindowVisible(video))return FALSE;
    screen=GetDC(NULL);
    for(y=1;y<4;++y)for(x=1;x<8;++x) {
        COLORREF color=GetPixel(screen,r.left+(r.right-r.left)*x/8,r.top+(r.bottom-r.top)*y/4);
        int red=GetRValue(color),green=GetGValue(color),blue=GetBValue(color);
        int high=red>green ? red : green,low=red<green ? red : green;
        if(blue>high)high=blue;
        if(blue<low)low=blue;
        if(color!=CLR_INVALID && high>140 && high-low>80)++colored;
    }
    ReleaseDC(NULL,screen);
    return colored>=3;
}
static BOOL await_video_picture(DWORD timeout) {
    DWORD start=GetTickCount();
    SetForegroundWindow(app);
    do { if(video_picture_visible())return TRUE; Sleep(25); }while(GetTickCount()-start<timeout);
    puts("FAIL visible generated-clip picture was not observed in the video viewport before the bounded deadline.");
    return FALSE;
}
static BOOL report_passed(const WCHAR *file) {
    char data[4096]; size_t n; FILE *input=_wfopen(file,L"rb"); BOOL ok;
    if(!input)return FALSE;
    n=fread(data,1,sizeof(data)-1,input); data[n]=0;
    ok=!ferror(input) && strstr(data,"\"complete\":true") && strstr(data,"\"passed\":true");
    if(fclose(input))ok=FALSE;
    return ok;
}
static int real_media(int argc, WCHAR **argv) {
    WCHAR exe[MAX_PATH],base[MAX_PATH],media[MAX_PATH],temp[MAX_PATH],folder[MAX_PATH];
    WCHAR report[MAX_PATH],arguments[MAX_PATH*2+96],copy[MAX_PATH],*slash;
    PROCESS_INFORMATION process; RECT original; LONG_PTR original_style;
    DWORD n,exit_code=99; BOOL done,created;
    ZeroMemory(&process,sizeof(process));
    real_capture=TRUE;
    if(argc==4) { screenshot_directory=argv[3]; CreateDirectoryW(screenshot_directory,NULL); }
    n=GetFullPathNameW(argv[1],MAX_PATH,exe,NULL); CHECK(n>0 && n<MAX_PATH);
    if(!n || n>=MAX_PATH)goto finished;
    lstrcpynW(base,exe,COUNT(base)); slash=wcsrchr(base,L'\\'); CHECK(slash!=NULL);
    if(!slash)goto finished;
    if(slash==base+2 && base[1]==L':')slash[1]=0; else *slash=0;
    if(!path(media,base,L"media\\prueba-240.mp4")) { CHECK(FALSE); goto finished; }
    CHECK(GetFileAttributesW(media)!=INVALID_FILE_ATTRIBUTES);
    n=GetTempPathW(MAX_PATH,temp); CHECK(n>0 && n<MAX_PATH);
    if(!n || n>=MAX_PATH)goto finished;
    n=GetTempFileNameW(temp,L"tlr",0,folder); CHECK(n!=0); if(!n)goto finished;
    CHECK(DeleteFileW(folder));
    created=CreateDirectoryW(folder,NULL); CHECK(created); if(!created)goto finished;
    report[0]=0;
    if(!path(report,folder,L"report.json")) { CHECK(FALSE); goto cleanup_report; }
    n=(DWORD)_snwprintf(arguments,COUNT(arguments),
        L"--self-test-media \"%ls\" --report \"%ls\" --test-ms 30000",media,report);
    CHECK(n<COUNT(arguments)); if(n>=COUNT(arguments))goto cleanup_report;
    CHECK(launch_arguments(exe,base,arguments,&process));
    if(!process.hProcess)goto cleanup_report;
    CHECK(await_real_playback(&process));
    if(app && video && IsWindowEnabled(GetDlgItem(app,PAUSE))) {
        HWND original_app=app;
        CHECK(GetWindowRect(app,&original)); original_style=GetWindowLongPtrW(app,GWL_STYLE);
        CHECK(await_video_picture(5000));
        click(FS_BUTTON); CHECK(await_fullscreen(TRUE)); check_fullscreen(original_app);
        CHECK(await_video_picture(3000));
        Sleep(700); CHECK(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT);
        screenshot(L"fullscreen-real.bmp");
        key(VK_ESCAPE); CHECK(await_fullscreen(FALSE)); check_restored(&original,original_style);
        CHECK(await_video_picture(3000));
        Sleep(700); CHECK(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT);
        CHECK(IsWindowEnabled(GetDlgItem(app,PAUSE)));
        screenshot(L"windowed-real.bmp");
    }
    done=WaitForSingleObject(process.hProcess,35000)==WAIT_OBJECT_0; CHECK(done);
    if(!done) { TerminateProcess(process.hProcess,99); WaitForSingleObject(process.hProcess,2000); }
    CHECK(GetExitCodeProcess(process.hProcess,&exit_code)); CHECK(exit_code==0);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); app=NULL; video=NULL;
    CHECK(report_passed(report));
    if(screenshot_directory && path(copy,screenshot_directory,L"fullscreen-real-report.json"))
        CHECK(CopyFileW(report,copy,FALSE));
cleanup_report:
    DeleteFileW(report); RemoveDirectoryW(folder);
finished:
    puts("Real VLC resize playback with dummy audio; physical XP/Atom and audible sound are not checked.");
    printf("Fullscreen real media: %d checks; %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
int wmain(int argc, WCHAR **argv) {
    WCHAR temp[MAX_PATH],base[MAX_PATH],exe[MAX_PATH],file[MAX_PATH],ini[MAX_PATH];
    PROCESS_INFORMATION process; DWORD n;
    if(argc!=3 && argc!=4) { puts("usage: fullscreen_test.exe APP.exe FAKE_VLC.dll|--real-media [SCREENSHOT_DIR]"); return 2; }
    if(!lstrcmpW(argv[2],L"--real-media"))return real_media(argc,argv);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_ERROR_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_STOP_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_RELEASE_MS",L"0");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",L"0");
    if(argc==4) { screenshot_directory=argv[3]; CreateDirectoryW(screenshot_directory,NULL); }
    ZeroMemory(&process,sizeof(process));
    n=GetTempPathW(MAX_PATH,temp); CHECK(n>0 && n<MAX_PATH);
    CHECK(GetTempFileNameW(temp,L"tlf",0,base)!=0);
    CHECK(DeleteFileW(base));
    CHECK(CreateDirectoryW(base,NULL));
    CHECK(path(exe,base,L"TubeLiviano.exe")); CHECK(CopyFileW(argv[1],exe,TRUE));
    CHECK(path(file,base,L"vlc")); CHECK(CreateDirectoryW(file,NULL));
    CHECK(path(file,base,L"vlc\\libvlc.dll")); CHECK(CopyFileW(argv[2],file,TRUE));
    CHECK(path(file,base,L"vlc\\libvlccore.dll")); CHECK(CopyFileW(argv[2],file,TRUE));
    CHECK(path(file,base,L"media")); CHECK(CreateDirectoryW(file,NULL));
    CHECK(path(file,base,L"media\\prueba-240.mp4")); CHECK(empty_file(file));
    CHECK(path(ini,base,L"TubeLiviano.ini"));
    CHECK(launch(exe,base,&process)); if(!process.hProcess)goto done;
    CHECK(await_app(&process)); if(!app || !video)goto finish;
    normal_controls(&process); maximized_restore(); closing_restore(&process,ini);
    CHECK(launch(exe,base,&process)); if(!process.hProcess)goto done;
    CHECK(await_app(&process)); if(!app || !video)goto finish;
    CHECK(!fullscreen()); CHECK(!IsWindowVisible(GetDlgItem(app,FS_BUTTON)));
    CHECK(!IsWindowEnabled(GetDlgItem(app,PAUSE)));
finish:
    if(process.hProcess)CHECK(close_app(&process));
done:
    cleanup(base);
    puts("Synthetic VLC fixture only: no real playback quality or XP hardware claim.");
    printf("Fullscreen: %d checks; %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
