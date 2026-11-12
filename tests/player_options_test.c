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
    CHECK(GetModuleFileNameW(NULL,base,MAX_PATH)>0);
    slash=wcsrchr(base,L'\\'); if(!slash)return 2; *slash=0;
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
    FreeLibrary(fixture);
    printf("%d player options/controller checks; %d failed (synthetic LibVLC)\n",checks,failed);
    return failed ? 1 : 0;
}
