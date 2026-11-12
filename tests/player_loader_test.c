/* Real libVLC DLL-search regression, with local audio/video decoding.
 * Compile alongside src/player.c, at the same architecture as the VLC stage.
 * Put this executable in a stage containing vlc/, media/prueba-240.mp4 and the
 * deliberately incompatible tests/vlc_decoy.c built as ROOT/libvlccore.dll.
 * That root DLL must not override vlc/libvlccore.dll. No network is used.
 */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../src/player.h"

static int checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)

static void pump(void)
{
    MSG message;
    while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}

static BOOL same_dll_directory(const WCHAR *wanted)
{
    WCHAR current[MAX_PATH];
    DWORD count=GetDllDirectoryW(MAX_PATH,current);
    return count && count<MAX_PATH && !_wcsicmp(current,wanted);
}

int main(void)
{
    WCHAR base[MAX_PATH], sentinel[MAX_PATH], missing[MAX_PATH], error[512];
    WCHAR expected_core[MAX_PATH], actual_core[MAX_PATH], *slash;
    char clip[1024];
    WCHAR wide_clip[MAX_PATH];
    HWND window;
    HMODULE core;
    PlayerStats stats;
    DWORD start;
    BOOL initialized, closed=FALSE, decoded=FALSE;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    CHECK(GetModuleFileNameW(NULL,base,MAX_PATH)>0);
    slash=wcsrchr(base,L'\\'); if (!slash) return 2; *slash=0;
    if (wcslen(base)>MAX_PATH-40) return 2;
    _snwprintf(sentinel,MAX_PATH,L"%ls\\sentinel",base);
    CHECK(CreateDirectoryW(sentinel,NULL) || GetLastError()==ERROR_ALREADY_EXISTS);
    CHECK(SetDllDirectoryW(sentinel));
    window=CreateWindowW(L"STATIC",L"TubeLiviano loader test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                         10,10,400,300,NULL,NULL,GetModuleHandleW(NULL),NULL);
    CHECK(window!=NULL);
    _snwprintf(missing,MAX_PATH,L"%ls\\missing-runtime",base);
    CHECK(!player_init(missing,window,TRUE,error,512));
    CHECK(error[0]!=0);
    CHECK(same_dll_directory(sentinel));
    CHECK(player_closed());
    CHECK(same_dll_directory(sentinel));

    /* A retry after the expected initialization failure must load the package's
     * own core, even though a wrong core DLL exists beside this executable. */
    initialized=player_init(base,window,TRUE,error,512);
    CHECK(initialized);
    if (!initialized) {
        printf("VLC initialization failed; Windows error %lu\n",(unsigned long)GetLastError());
        wprintf(L"VLC detail: %ls\n",error);
    }
    CHECK(same_dll_directory(sentinel));
    if (initialized) {
        core=GetModuleHandleW(L"libvlccore.dll");
        CHECK(core!=NULL);
        CHECK(core && GetModuleFileNameW(core,actual_core,MAX_PATH)>0);
        _snwprintf(expected_core,MAX_PATH,L"%ls\\vlc\\libvlccore.dll",base);
        CHECK(core && !_wcsicmp(actual_core,expected_core));
        _snwprintf(wide_clip,MAX_PATH,L"%ls\\media\\prueba-240.mp4",base);
        CHECK(WideCharToMultiByte(CP_UTF8,0,wide_clip,-1,clip,sizeof(clip),NULL,NULL)!=0);
        CHECK(player_open(clip,NULL,TRUE,error,512));
        start=GetTickCount();
        do {
            pump();
            if (player_stats(&stats) && stats.decoded_video>0 && stats.decoded_audio>0 &&
                stats.displayed_pictures>0 && stats.played_abuffers>0) {
                decoded=TRUE; break;
            }
            Sleep(20);
        } while ((DWORD)(GetTickCount()-start)<10000);
        CHECK(decoded);
        printf("Decoded video=%d audio=%d displayed=%d played_audio=%d\n",
               stats.decoded_video,stats.decoded_audio,stats.displayed_pictures,stats.played_abuffers);
        player_close();
        start=GetTickCount();
        do {
            pump();
            if (player_closed()) { closed=TRUE; break; }
            Sleep(10);
        } while ((DWORD)(GetTickCount()-start)<7000);
        CHECK(closed);
    } else CHECK(player_closed());
    CHECK(same_dll_directory(sentinel));
    DestroyWindow(window);
    SetDllDirectoryW(NULL); /* Test process cleanup; product must not mutate it. */
    RemoveDirectoryW(sentinel);
    printf("%d player loader checks; %d failed\n",checks,failed);
    return failed?1:0;
}
