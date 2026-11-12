/* Deterministic controller/options regression using tests/fake_vlc.c only.
 * Stage beside vlc/libvlc.dll built from that test fixture. This verifies API
 * arguments and asynchronous lifetime rules; it makes no decoding claim. */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../src/player.h"

static int checks, failed;
static LONG (__cdecl *value)(int);
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)

static BOOL wait_state(int wanted)
{
    DWORD start=GetTickCount();
    do {
        if(player_state()==wanted)return TRUE;
        Sleep(10);
    } while(GetTickCount()-start<5000);
    return FALSE;
}
static BOOL wait_closed(void)
{
    DWORD start=GetTickCount();
    do {
        if(player_closed())return TRUE;
        Sleep(10);
    } while(GetTickCount()-start<5000);
    return FALSE;
}
static void check_options(int cache,int prefetch,int threads,int loudness,int mute)
{
    CHECK(value(2)==cache); CHECK(value(3)==prefetch); CHECK(value(4)==threads);
    CHECK(value(5)==loudness); CHECK(value(6)==mute);
    CHECK(value(7)==cache); CHECK(value(12)==threads);
}
static BOOL wait_value(int key, LONG wanted)
{
    DWORD start=GetTickCount();
    do { if(value(key)==wanted)return TRUE; Sleep(10); }while(GetTickCount()-start<5000);
    return FALSE;
}
static void async_missing_retry(const WCHAR *source)
{
    WCHAR temp[MAX_PATH],base[MAX_PATH],folder[MAX_PATH],file[MAX_PATH],original[MAX_PATH],error[512];
    DWORD start;
    CHECK(GetTempPathW(MAX_PATH,temp)>0);
    CHECK(GetTempFileNameW(temp,L"tla",0,base)!=0); CHECK(DeleteFileW(base));
    CHECK(CreateDirectoryW(base,NULL));
    start=GetTickCount(); CHECK(player_prepare(base,NULL,TRUE,error,512));
    CHECK(GetTickCount()-start<150); CHECK(wait_state(7));
    CHECK(player_startup_error(error,512)); CHECK(wcsstr(error,L"libvlc.dll")!=NULL);
    /* Fix the fixture folder in place and retry on the same controller. */
    _snwprintf(folder,MAX_PATH,L"%ls\\vlc",base); CHECK(CreateDirectoryW(folder,NULL));
    _snwprintf(file,MAX_PATH,L"%ls\\libvlc.dll",folder);
    _snwprintf(original,MAX_PATH,L"%ls\\vlc\\libvlc.dll",source);
    CHECK(CopyFileW(original,file,TRUE));
    CHECK(player_prepare(base,NULL,TRUE,error,512));
    CHECK(player_open("fixed-loader.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    CHECK(!player_startup_error(error,512));
    player_close(); CHECK(wait_closed());
    CHECK(DeleteFileW(file)); CHECK(RemoveDirectoryW(folder)); CHECK(RemoveDirectoryW(base));
}
static void async_lifecycle(const WCHAR *base, LONG main_thread)
{
    WCHAR error[512]; PlayerStats stats;
    DWORD start; LONG before,instances,releases;
    player_configure(NULL);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_INIT_MS",L"1000");
    before=value(14); instances=value(0);
    start=GetTickCount(); CHECK(player_prepare(base,NULL,TRUE,error,512));
    CHECK(GetTickCount()-start<150); CHECK(player_state()==1); CHECK(player_preparing()); CHECK(!player_stats(&stats));
    CHECK(player_open("queued-during-bootstrap.mp4",NULL,TRUE,error,512));
    CHECK(wait_value(16,1)); CHECK(player_preparing()); CHECK(wait_state(3)); CHECK(!player_preparing());
    CHECK(value(0)==instances+1); CHECK(value(14)==before+1); CHECK(value(8)!=main_thread);
    CHECK(!player_startup_error(error,512)); player_close(); CHECK(wait_closed());

    /* Cancel and Close must not resurrect an OPEN queued during slow init. */
    before=value(14);
    start=GetTickCount(); CHECK(player_prepare(base,NULL,TRUE,error,512));
    CHECK(GetTickCount()-start<150); CHECK(wait_value(16,1));
    CHECK(player_open("must-not-play-after-cancel.mp4",NULL,TRUE,error,512));
    start=GetTickCount(); player_stop(); CHECK(GetTickCount()-start<150);
    CHECK(wait_state(5)); CHECK(value(14)==before); CHECK(!player_stats(&stats));
    player_close(); CHECK(wait_closed());

    before=value(14); releases=value(1);
    CHECK(player_prepare(base,NULL,TRUE,error,512)); CHECK(wait_value(16,1));
    CHECK(player_open("must-not-play-after-close.mp4",NULL,TRUE,error,512));
    start=GetTickCount(); player_close(); CHECK(GetTickCount()-start<150);
    CHECK(!player_prepare(base,NULL,TRUE,error,512)); CHECK(!player_closed());
    CHECK(wait_closed()); CHECK(!player_preparing()); CHECK(value(14)==before); CHECK(value(1)==releases+1);
    CHECK(value(9)!=main_thread); CHECK(value(10)==0);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_INIT_MS",NULL);

    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",L"1");
    CHECK(player_prepare(base,NULL,TRUE,error,512)); CHECK(wait_state(7));
    CHECK(player_startup_error(error,512)); CHECK(error[0]!=0);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",NULL);
    CHECK(player_open("retry-async-core.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    CHECK(!player_startup_error(error,512)); player_close(); CHECK(wait_closed()); CHECK(value(10)==0);
    {
        WCHAR temp[MAX_PATH],trigger[MAX_PATH];
        CHECK(GetTempPathW(MAX_PATH,temp)>0);
        CHECK(GetTempFileNameW(temp,L"tlf",0,trigger)!=0);
        SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_PLAY_FILE",trigger);
        CHECK(player_prepare(base,NULL,TRUE,error,512));
        CHECK(player_open("one-shot-stream-failure.mp4",NULL,TRUE,error,512)); CHECK(wait_state(7));
        CHECK(!player_startup_error(error,512));
        CHECK(GetFileAttributesW(trigger)==INVALID_FILE_ATTRIBUTES);
        CHECK(player_open("fresh-stream-retry.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
        player_close(); CHECK(wait_closed());
        SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_PLAY_FILE",NULL);
    }
}
static void private_cache_options(const WCHAR *base)
{
    WCHAR folder[MAX_PATH],file[MAX_PATH],error[512],inherited[32768];
    DWORD old_length=GetEnvironmentVariableW(L"VLC_PLUGIN_PATH",inherited,32768);
    HANDLE sentinel; BOOL made;
    _snwprintf(folder,MAX_PATH,L"%ls\\vlc\\plugins",base);
    made=CreateDirectoryW(folder,NULL);
    CHECK(made || GetLastError()==ERROR_ALREADY_EXISTS);
    _snwprintf(file,MAX_PATH,L"%ls\\plugins.dat",folder);
    /* This directory belongs to the fake DLL test stage, never real VLC. */
    CHECK(GetFileAttributesW(file)==INVALID_FILE_ATTRIBUTES);
    SetEnvironmentVariableW(L"VLC_PLUGIN_PATH",NULL);
    CHECK(player_prepare(base,NULL,TRUE,error,512)); CHECK(player_open("first-cache.mp4",NULL,TRUE,error,512));
    CHECK(wait_state(3)); CHECK(value(15)==1); player_close(); CHECK(wait_closed());
    /* An existence-only sentinel tests our flag, not validity of a VLC cache. */
    sentinel=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    CHECK(sentinel!=INVALID_HANDLE_VALUE); if(sentinel!=INVALID_HANDLE_VALUE)CloseHandle(sentinel);
    CHECK(player_prepare(base,NULL,TRUE,error,512)); CHECK(player_open("cached.mp4",NULL,TRUE,error,512));
    CHECK(wait_state(3)); CHECK(value(15)==0); player_close(); CHECK(wait_closed());
    CHECK(DeleteFileW(file));
    SetEnvironmentVariableW(L"VLC_PLUGIN_PATH",L"Z:\\another-installation\\plugins");
    CHECK(player_prepare(base,NULL,TRUE,error,512)); CHECK(player_open("foreign-roots.mp4",NULL,TRUE,error,512));
    CHECK(wait_state(3)); CHECK(value(15)==0); player_close(); CHECK(wait_closed());
    SetEnvironmentVariableW(L"VLC_PLUGIN_PATH",old_length ? inherited : NULL);
    if(made)CHECK(RemoveDirectoryW(folder));
}
int main(void)
{
    WCHAR base[MAX_PATH],path[MAX_PATH],error[512],*slash;
    HMODULE fixture;
    FARPROC export;
    PlayerStats stats;
    PlayerOptions options={900,64,2,41,FALSE};
    LONG main_thread=(LONG)GetCurrentThreadId(),pauses;
    DWORD start;
    int i;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_INIT_MS",NULL);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",NULL);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_PLAY_FILE",NULL);
    CHECK(GetModuleFileNameW(NULL,base,MAX_PATH)>0);
    slash=wcsrchr(base,L'\\'); if(!slash)return 2; *slash=0;
    async_missing_retry(base);
    _snwprintf(path,MAX_PATH,L"%ls\\vlc\\libvlc.dll",base);
    fixture=LoadLibraryExW(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
    CHECK(fixture!=NULL); if(!fixture)return 2;
    export=GetProcAddress(fixture,"tl_fake_vlc_value"); memcpy(&value,&export,sizeof(value));
    CHECK(value!=NULL); if(!value)return 2;
    player_configure(NULL);
    CHECK(player_init(base,NULL,TRUE,error,512));
    CHECK(value(0)==1); CHECK(value(2)==450); CHECK(value(3)==128); CHECK(value(4)==1);
    CHECK(value(5)==80); CHECK(value(6)==FALSE);
    CHECK(player_open("test-one.mp4",NULL,TRUE,error,512));
    CHECK(wait_state(3)); check_options(450,128,1,80,FALSE);

    /* Audio-only changes act on this player without recreating its core. */
    options.cache_ms=450; options.prefetch_kib=128; options.decoder_threads=1;
    options.volume=23; options.muted=TRUE;
    player_configure(&options);
    CHECK(value(0)==1); CHECK(value(5)==23); CHECK(value(6)==TRUE);
    CHECK(player_state()==3);

    SetEnvironmentVariableW(L"TL_FAKE_VLC_STOP_MS",L"700");
    SetEnvironmentVariableW(L"TL_FAKE_VLC_RELEASE_MS",L"250");
    options.cache_ms=900; options.prefetch_kib=64; options.decoder_threads=2;
    options.volume=41; options.muted=FALSE;
    start=GetTickCount(); player_configure(&options);
    CHECK(GetTickCount()-start<150); CHECK(value(0)==1);
    pauses=value(13);
    start=GetTickCount(); CHECK(player_open("test-two.mp4",NULL,TRUE,error,512));
    player_pause(); CHECK(GetTickCount()-start<150);
    /* Stats/state must not call VLC while stop/release is in progress. */
    start=GetTickCount();
    for(i=0;i<100;++i) { CHECK(player_state()==1); CHECK(!player_stats(&stats)); }
    CHECK(GetTickCount()-start<150);
    CHECK(wait_state(3)); CHECK(value(13)==pauses);
    CHECK(value(0)==2); CHECK(value(1)==1);
    CHECK(value(8)!=main_thread); CHECK(value(9)==value(8)); CHECK(value(11)==value(8));
    CHECK(value(10)==0); check_options(900,64,2,41,FALSE);

    /* main calls init before each OPEN, including a retry after core failure.
     * Keep the existing worker/gate and let it rebuild the failed core. */
    options.cache_ms=1000;
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",L"1");
    player_configure(&options);
    CHECK(player_open("failure.mp4",NULL,TRUE,error,512)); CHECK(wait_state(7));
    CHECK(value(0)==2); CHECK(value(1)==2);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_NEW",NULL);
    CHECK(player_init(base,NULL,TRUE,error,512)); CHECK(value(0)==2);
    CHECK(player_open("retry.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    CHECK(value(0)==3); CHECK(value(8)!=main_thread); check_options(1000,64,2,41,FALSE);

    options.cache_ms=-1; options.prefetch_kib=65; options.decoder_threads=3;
    options.volume=500; options.muted=7;
    player_configure(&options); CHECK(value(5)==100); CHECK(value(6)==TRUE);
    CHECK(player_open("test-three.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    check_options(250,128,1,100,TRUE);
    options.cache_ms=9000; options.prefetch_kib=256; options.decoder_threads=0;
    options.volume=-1; options.muted=FALSE;
    player_configure(&options);
    CHECK(player_open("test-four.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    check_options(3000,256,1,0,FALSE);
    player_configure(NULL);
    CHECK(player_open("test-five.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    check_options(450,128,1,80,FALSE);

    start=GetTickCount(); player_close(); CHECK(GetTickCount()-start<150);
    CHECK(!player_stats(&stats)); CHECK(!player_closed());
    CHECK(wait_closed()); CHECK(value(10)==0); CHECK(value(1)==6);
    CHECK(value(9)!=main_thread); CHECK(player_closed());
    SetEnvironmentVariableW(L"TL_FAKE_VLC_STOP_MS",NULL);
    SetEnvironmentVariableW(L"TL_FAKE_VLC_RELEASE_MS",NULL);
    /* Reinitialization resets controller lifecycle state after a clean close. */
    CHECK(player_init(base,NULL,TRUE,error,512));
    CHECK(player_open("test-six.mp4",NULL,TRUE,error,512)); CHECK(wait_state(3));
    player_close(); CHECK(wait_closed()); CHECK(value(10)==0);
    async_lifecycle(base,main_thread);
    private_cache_options(base);
    FreeLibrary(fixture);
    printf("%d player options/controller checks; %d failed (synthetic LibVLC)\n",checks,failed);
    return failed ? 1 : 0;
}
