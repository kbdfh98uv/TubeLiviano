/* Test-only deterministic LibVLC stand-in. These counters are synthetic and
 * must never be described as real multimedia playback or shipped as VLC. */
#define _WIN32_WINNT 0x0501
#include "../src/player.h"
#include <stdlib.h>
#include <string.h>

#define API __declspec(dllexport)
static volatile LONG running, paused, force_hang, slow_audio;
static volatile LONG instances, released, cache_ms, prefetch_kib, decoder_threads;
static volatile LONG volume, muted, media_cache, media_threads, live_player, live_media;
static volatile LONG create_thread, release_thread, stop_thread, stopping, invalid_access, pauses;
static DWORD started;
static volatile LONG initializing, reset_cache, plays;
static DWORD option(const WCHAR *name)
{
    WCHAR text[32];
    return GetEnvironmentVariableW(name,text,32) ? (DWORD)_wtoi(text) : 0;
}
API void *__cdecl libvlc_new(int argc,const char *const *argv)
{
    int i;
    initializing=1; Sleep(option(L"TL_FAKE_VLC_INIT_MS")); initializing=0;
    if(option(L"TL_FAKE_VLC_FAIL_NEW"))return NULL;
    reset_cache=0;
    for(i=0;i<argc;++i) {
        if(!strcmp(argv[i],"--reset-plugins-cache"))reset_cache=1;
        if(!strncmp(argv[i],"--network-caching=",18))cache_ms=atoi(argv[i]+18);
        if(!strncmp(argv[i],"--prefetch-buffer-size=",23))prefetch_kib=atoi(argv[i]+23);
        if(!strncmp(argv[i],"--avcodec-threads=",18))decoder_threads=atoi(argv[i]+18);
    }
    create_thread=(LONG)GetCurrentThreadId(); InterlockedIncrement(&instances);return (void *)1;
}
API void __cdecl libvlc_release(void *instance) { (void)instance; release_thread=(LONG)GetCurrentThreadId(); InterlockedIncrement(&released); Sleep(option(L"TL_FAKE_VLC_RELEASE_MS")); }
API void *__cdecl libvlc_media_player_new(void *instance) { (void)instance;live_player=1;return (void *)2; }
API void __cdecl libvlc_media_player_release(void *player) { (void)player;live_player=0; }
API void __cdecl libvlc_media_player_set_hwnd(void *player,void *window) { (void)player;(void)window; }
API void *__cdecl libvlc_media_new_location(void *instance,const char *path) { (void)instance;(void)path;live_media=1;return (void *)3; }
API void *__cdecl libvlc_media_new_path(void *instance,const char *path) { return libvlc_media_new_location(instance,path); }
API void __cdecl libvlc_media_add_option(void *media,const char *text)
{
    (void)media;
    if(!strncmp(text,":network-caching=",17))media_cache=atoi(text+17);
    if(!strncmp(text,":avcodec-threads=",17))media_threads=atoi(text+17);
}
API void __cdecl libvlc_media_release(void *media) { (void)media;live_media=0; }
API void __cdecl libvlc_media_player_set_media(void *player,void *media) { (void)player;(void)media; }
API int __cdecl libvlc_media_player_play(void *player)
{
    WCHAR trigger[MAX_PATH]; DWORD n,attrs;
    (void)player; InterlockedIncrement(&plays);
    InterlockedExchange(&force_hang,0); InterlockedExchange(&slow_audio,0);
    n=GetEnvironmentVariableW(L"TL_FAKE_VLC_FAIL_PLAY_FILE",trigger,MAX_PATH);
    if(n && n<MAX_PATH) {
        attrs=GetFileAttributesW(trigger);
        if(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY)) {
            DeleteFileW(trigger); return -1;
        }
    }
    n=GetEnvironmentVariableW(L"TL_FAKE_VLC_HANG_PLAY_FILE",trigger,MAX_PATH);
    if(n && n<MAX_PATH) {
        attrs=GetFileAttributesW(trigger);
        if(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY) && DeleteFileW(trigger))
            InterlockedExchange(&force_hang,1);
    }
    n=GetEnvironmentVariableW(L"TL_FAKE_VLC_SLOW_AUDIO_FILE",trigger,MAX_PATH);
    if(n && n<MAX_PATH) {
        attrs=GetFileAttributesW(trigger);
        if(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY) && DeleteFileW(trigger))
            InterlockedExchange(&slow_audio,1);
    }
    started=GetTickCount();InterlockedExchange(&paused,0);InterlockedExchange(&running,1);return 0;
}
API void __cdecl libvlc_media_player_stop(void *player)
{
    (void)player;
    stop_thread=(LONG)GetCurrentThreadId(); stopping=1;
    if(InterlockedExchange(&running,0))Sleep(option(L"TL_FAKE_VLC_STOP_MS"));
    stopping=0;
}
API void __cdecl libvlc_media_player_pause(void *player) { (void)player;InterlockedIncrement(&pauses);InterlockedExchange(&paused,!paused); }
API int __cdecl libvlc_media_player_get_state(void *player)
{
    DWORD fail=option(L"TL_FAKE_VLC_ERROR_MS");(void)player;
    if(!live_player || stopping)InterlockedIncrement(&invalid_access);
    if(!running)return 5;
    if(force_hang)return 1;
    if(fail && GetTickCount()-started>=fail)return 7;
    return paused ? 4 : 3;
}
API int __cdecl libvlc_media_get_stats(void *media,PlayerStats *stats)
{
    (void)media; memset(stats,0,sizeof(*stats));
    if(!live_media || stopping)InterlockedIncrement(&invalid_access);
    if(running && !force_hang && GetTickCount()-started>=100) {
        stats->decoded_video=12;stats->decoded_audio=24;stats->displayed_pictures=10;
        stats->played_abuffers=slow_audio && GetTickCount()-started<8000 ? 0 : 20;stats->read_bytes=1024;
    }
    return 1;
}
API int __cdecl libvlc_audio_set_volume(void *player,int value) { (void)player;if(!live_player)InterlockedIncrement(&invalid_access);volume=value;return 0; }
API void __cdecl libvlc_audio_set_mute(void *player,int value) { (void)player;if(!live_player)InterlockedIncrement(&invalid_access);muted=value; }
/* Private test diagnostics; no production code loads these exports. */
API LONG __cdecl tl_fake_vlc_value(int key)
{
    switch(key) {
        case 0:return instances; case 1:return released; case 2:return cache_ms;
        case 3:return prefetch_kib; case 4:return decoder_threads; case 5:return volume;
        case 6:return muted; case 7:return media_cache; case 8:return create_thread;
        case 9:return release_thread; case 10:return invalid_access; case 11:return stop_thread;
        case 12:return media_threads; case 13:return pauses;
        case 14:return plays; case 15:return reset_cache; case 16:return initializing;
    }
    return -1;
}
API const char *__cdecl libvlc_errmsg(void) { return "Test-only synthetic LibVLC error"; }
