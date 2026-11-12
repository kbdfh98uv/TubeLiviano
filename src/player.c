#define _WIN32_WINNT 0x0501
#include "player.h"
#include <stdio.h>
#include <string.h>

static HMODULE dll;
static void *vlc, *mp, *media;
static HWND target;
static HANDLE controller, changed;
static CRITICAL_SECTION gate;
static int pending_command;
static BOOL busy, shutting_down, failed, startup_failed, preparing;
static WCHAR portable_base[MAX_PATH], library_path[MAX_PATH], startup_message[512];
static BOOL test_mode, core_dirty;
static PlayerOptions configured = {450,128,1,80,FALSE};
static PlayerOptions active;
static char trust[1024];
static char next_video[24576], next_audio[16384];
static BOOL next_local;
enum { CMD_OPEN=1, CMD_STOP, CMD_PAUSE, CMD_CLOSE, CMD_PREPARE };
static DWORD WINAPI control_player(void *unused);
#define FN(ret, name, args) static ret (__cdecl *name) args
FN(void *, libvlc_new, (int, const char *const *));
FN(void, libvlc_release, (void *));
FN(void *, libvlc_media_player_new, (void *));
FN(void, libvlc_media_player_release, (void *));
FN(void, libvlc_media_player_set_hwnd, (void *, void *));
FN(void *, libvlc_media_new_location, (void *, const char *));
FN(void *, libvlc_media_new_path, (void *, const char *));
FN(void, libvlc_media_add_option, (void *, const char *));
FN(void, libvlc_media_release, (void *));
FN(void, libvlc_media_player_set_media, (void *, void *));
FN(int, libvlc_media_player_play, (void *));
FN(void, libvlc_media_player_stop, (void *));
FN(void, libvlc_media_player_pause, (void *));
FN(int, libvlc_media_player_get_state, (void *));
FN(int, libvlc_media_get_stats, (void *, PlayerStats *));
FN(int, libvlc_audio_set_volume, (void *, int));
FN(void, libvlc_audio_set_mute, (void *, int));
FN(const char *, libvlc_errmsg, (void));

static BOOL same_core(const PlayerOptions *a, const PlayerOptions *b) {
    return a->cache_ms==b->cache_ms && a->prefetch_kib==b->prefetch_kib &&
           a->decoder_threads==b->decoder_threads;
}
static void apply_audio(const PlayerOptions *options) {
    if (mp) {
        libvlc_audio_set_volume(mp,options->volume);
        libvlc_audio_set_mute(mp,options->muted);
    }
}
void player_configure(const PlayerOptions *options) {
    PlayerOptions value={450,128,1,80,FALSE};
    if (options) {
        value=*options;
        if (value.cache_ms<250) value.cache_ms=250;
        if (value.cache_ms>3000) value.cache_ms=3000;
        if (value.prefetch_kib!=64 && value.prefetch_kib!=128 && value.prefetch_kib!=256) value.prefetch_kib=128;
        if (value.decoder_threads!=1 && value.decoder_threads!=2) value.decoder_threads=1;
        if (value.volume<0) value.volume=0;
        if (value.volume>100) value.volume=100;
        value.muted=!!value.muted;
    }
    if (controller) EnterCriticalSection(&gate);
    configured=value;
    core_dirty=!same_core(&configured,&active);
    if (controller && !busy && !pending_command && !shutting_down) apply_audio(&configured);
    if (controller) LeaveCriticalSection(&gate);
}

static BOOL cache_reset_allowed(void) {
    WCHAR folder[MAX_PATH], cache[MAX_PATH];
    DWORD attributes;
    /* Reset traverses every VLC_PLUGIN_PATH root too. Never write another
     * installation's plugin index; an inherited custom path keeps default scan. */
    if(GetEnvironmentVariableW(L"VLC_PLUGIN_PATH",NULL,0)>1) return FALSE;
    _snwprintf(folder,MAX_PATH,L"%ls\\vlc\\plugins",portable_base);
    attributes=GetFileAttributesW(folder);
    if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
    _snwprintf(cache,MAX_PATH,L"%ls\\plugins.dat",folder);
    return GetFileAttributesW(cache)==INVALID_FILE_ATTRIBUTES;
}
static BOOL create_core(const PlayerOptions *options) {
    char cache[40], prefetch[40], threads[40];
    const char *args[] = {"--ignore-config", "--no-media-library", "--no-video-title-show",
        "--no-osd", "--no-sub-autodetect-file", threads,
        "--avcodec-hw=none", cache, "--file-caching=150",
        "--vout=wingdi", "--stats", prefetch,
        "--prefetch-read-size=16384", "--prefetch-seek-threshold=32768", trust, "--aout=dummy", NULL};
    int argc=(int)(sizeof(args)/sizeof(args[0]))-1-(test_mode ? 0 : 1);
    snprintf(cache,sizeof(cache),"--network-caching=%d",options->cache_ms);
    snprintf(prefetch,sizeof(prefetch),"--prefetch-buffer-size=%d",options->prefetch_kib);
    snprintf(threads,sizeof(threads),"--avcodec-threads=%d",options->decoder_threads);
    /* Test mode uses a dummy audio sink on headless Wine. */
    /* The first real play writes a native, relative-path plugin index. Future
     * opens use VLC's normal cache validation/scanning, including safe fallback
     * for stale caches. Failed writes in read-only portable folders are nonfatal. */
    if(cache_reset_allowed()) args[argc++]="--reset-plugins-cache";
    vlc=libvlc_new(argc,args);
    if (!vlc) return FALSE;
    mp=libvlc_media_player_new(vlc);
    if (!mp) { libvlc_release(vlc); vlc=NULL; return FALSE; }
    libvlc_media_player_set_hwnd(mp,target);
    apply_audio(options);
    return TRUE;
}
static void release_core(void) {
    if (mp) libvlc_media_player_release(mp);
    if (vlc) libvlc_release(vlc);
    mp=vlc=NULL;
}

static void error(WCHAR *out, size_t cap, const WCHAR *fallback) {
    const char *s = libvlc_errmsg ? libvlc_errmsg() : NULL;
    if (!s || !MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap)) {
        lstrcpynW(out, fallback, (int)cap);
    }
}

static BOOL setup_paths(const WCHAR *base, HWND video, BOOL test, WCHAR *err, size_t cap) {
    WCHAR certfolder[MAX_PATH];
    target=video; test_mode=test;
    if(!base || !*base || wcslen(base)+25>=MAX_PATH) {
        lstrcpynW(err,L"Extraé el ZIP en una ruta más corta (menos de 230 caracteres).",(int)cap);
        return FALSE;
    }
    lstrcpyW(portable_base,base);
    _snwprintf(certfolder,MAX_PATH,L"%ls\\vlc\\certs",base);
    strcpy(trust,"--gnutls-dir-trust=");
    if(!WideCharToMultiByte(CP_UTF8,0,certfolder,-1,trust+19,sizeof(trust)-19,NULL,NULL)) {
        lstrcpynW(err,L"No se pudo configurar la carpeta de certificados.",(int)cap); return FALSE;
    }
    _snwprintf(library_path,MAX_PATH,L"%ls\\vlc\\libvlc.dll",base);
    startup_failed=preparing=FALSE; startup_message[0]=0;
    return TRUE;
}
static BOOL load_backend(WCHAR *err, size_t cap) {
    if(dll) return TRUE;
    libvlc_errmsg=NULL;
    /* XP-compatible absolute-DLL dependency lookup, without changing the
     * process or child DLL search directory. */
    dll=LoadLibraryExW(library_path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!dll) {
        lstrcpynW(err,L"No se pudo cargar vlc\\libvlc.dll x86. Extraé TODO el ZIP.",(int)cap);
        return FALSE;
    }
#define LOAD(name) do { FARPROC p = GetProcAddress(dll, #name); memcpy(&name, &p, sizeof(name)); if (!name) goto missing; } while (0)
    LOAD(libvlc_new); LOAD(libvlc_release); LOAD(libvlc_media_player_new);
    LOAD(libvlc_media_player_release); LOAD(libvlc_media_player_set_hwnd);
    LOAD(libvlc_media_new_location); LOAD(libvlc_media_new_path); LOAD(libvlc_media_add_option);
    LOAD(libvlc_media_release); LOAD(libvlc_media_player_set_media); LOAD(libvlc_media_player_play);
    LOAD(libvlc_media_player_stop); LOAD(libvlc_media_player_pause);
    LOAD(libvlc_media_player_get_state); LOAD(libvlc_media_get_stats); LOAD(libvlc_errmsg);
    LOAD(libvlc_audio_set_volume); LOAD(libvlc_audio_set_mute);
#undef LOAD
    return TRUE;
missing:
    lstrcpynW(err,L"La biblioteca VLC no tiene la API de VLC 3 esperada.",(int)cap);
    FreeLibrary(dll); dll=NULL; libvlc_errmsg=NULL;
    return FALSE;
}
static BOOL start_controller(int command, WCHAR *err, size_t cap) {
    InitializeCriticalSection(&gate);
    pending_command=command; busy=shutting_down=failed=FALSE; preparing=command==CMD_PREPARE;
    changed=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(changed) controller=CreateThread(NULL,0,control_player,NULL,0,NULL);
    if(!controller) {
        if(changed) CloseHandle(changed);
        changed=NULL; DeleteCriticalSection(&gate);
        lstrcpynW(err,L"No se pudo crear el hilo de reproducción.",(int)cap); return FALSE;
    }
    /* The worker cannot run a command before controller and gate are ready. */
    if(command) SetEvent(changed);
    return TRUE;
}
BOOL player_init(const WCHAR *base, HWND video, BOOL test, WCHAR *err, size_t cap) {
    /* Preserve the synchronous loader/error contract for embedders/tests. The
     * GUI uses player_prepare so both DLL loading and libvlc_new stay off it. */
    if(controller) {
        if(!shutting_down) return TRUE;
        lstrcpynW(err,L"El reproductor se está cerrando.",(int)cap); return FALSE;
    }
    if(dll) { FreeLibrary(dll); dll=NULL; libvlc_errmsg=NULL; }
    if(!setup_paths(base,video,test,err,cap)) return FALSE;
    if(!load_backend(err,cap)) return FALSE;
    if(!create_core(&configured)) { error(err,cap,L"VLC no pudo inicializar el reproductor."); return FALSE; }
    active=configured; core_dirty=FALSE;
    if(!start_controller(0,err,cap)) { release_core(); return FALSE; }
    return TRUE;
}
BOOL player_prepare(const WCHAR *base, HWND video, BOOL test, WCHAR *err, size_t cap) {
    if(controller) {
        BOOL retry=FALSE,closing;
        EnterCriticalSection(&gate);
        closing=shutting_down;
        if(!closing && !busy && !pending_command && startup_failed) {
            startup_failed=failed=FALSE; startup_message[0]=0;
            pending_command=CMD_PREPARE; preparing=TRUE; retry=TRUE;
        }
        LeaveCriticalSection(&gate);
        if(closing) { lstrcpynW(err,L"El reproductor se está cerrando.",(int)cap); return FALSE; }
        if(retry) SetEvent(changed);
        return TRUE;
    }
    if(dll) { release_core(); FreeLibrary(dll); dll=NULL; libvlc_errmsg=NULL; }
    if(!setup_paths(base,video,test,err,cap)) return FALSE;
    return start_controller(CMD_PREPARE,err,cap);
}
BOOL player_preparing(void) {
    BOOL result;
    if(!controller) return FALSE;
    EnterCriticalSection(&gate); result=preparing; LeaveCriticalSection(&gate);
    return result;
}
BOOL player_startup_error(WCHAR *err, size_t cap) {
    BOOL result=FALSE;
    if(!controller) return FALSE;
    EnterCriticalSection(&gate);
    if(!busy && !pending_command && startup_failed) {
        result=TRUE;
        if(err && cap) lstrcpynW(err,startup_message,(int)cap);
    }
    LeaveCriticalSection(&gate);
    return result;
}
static BOOL command_superseded(void) {
    BOOL result;
    EnterCriticalSection(&gate);
    result=shutting_down || pending_command!=0;
    LeaveCriticalSection(&gate);
    return result;
}
static BOOL ensure_core(const PlayerOptions *options) {
    WCHAR detail[512];
    BOOL ok=TRUE;
    EnterCriticalSection(&gate); preparing=TRUE; LeaveCriticalSection(&gate);
    if(!dll) ok=load_backend(detail,sizeof(detail)/sizeof(detail[0]));
    if(ok && (!vlc || !mp)) {
        ok=create_core(options);
        if(!ok) error(detail,sizeof(detail)/sizeof(detail[0]),L"VLC no pudo inicializar el reproductor.");
    }
    EnterCriticalSection(&gate);
    startup_failed=!ok; preparing=FALSE;
    if(!ok) lstrcpynW(startup_message,detail,sizeof(startup_message)/sizeof(startup_message[0]));
    else { startup_message[0]=0; active=*options; core_dirty=!same_core(&configured,&active); }
    LeaveCriticalSection(&gate);
    return ok;
}

static void stop_sync(void) {
    if (mp) libvlc_media_player_stop(mp);
    EnterCriticalSection(&gate);
    if (media) { libvlc_media_release(media); media = NULL; }
    LeaveCriticalSection(&gate);
}
static BOOL open_sync(const char *video, const char *audio, BOOL local) {
    char opt[16416];
    void *newmedia;
    PlayerOptions options;
    BOOL recreate;
    stop_sync();
    EnterCriticalSection(&gate);
    options=configured; recreate=core_dirty || !mp;
    LeaveCriticalSection(&gate);
    if (recreate) {
        EnterCriticalSection(&gate); preparing=TRUE; LeaveCriticalSection(&gate);
        /* Stop/release can wait for video callbacks; keep it on this worker. */
        release_core();
        if (!ensure_core(&options)) return FALSE;
    }
    /* Cancel/Close/newer Open may arrive during slow plugin initialization. */
    if(command_superseded()) return TRUE;
    newmedia = local ? libvlc_media_new_path(vlc, video) : libvlc_media_new_location(vlc, video);
    if (!newmedia) return FALSE;
    EnterCriticalSection(&gate);
    media = newmedia;
    LeaveCriticalSection(&gate);
    snprintf(opt,sizeof(opt),":network-caching=%d",options.cache_ms);
    libvlc_media_add_option(media,opt);
    /* set_hwnd resets VLC 3's inherited vout/hw options; set per-media afterwards. */
    libvlc_media_add_option(media, ":vout=wingdi");
    libvlc_media_add_option(media, ":avcodec-hw=none");
    snprintf(opt,sizeof(opt),":avcodec-threads=%d",options.decoder_threads);
    libvlc_media_add_option(media,opt);
    if (audio && *audio) {
        snprintf(opt, sizeof(opt), ":input-slave=%s", audio);
        libvlc_media_add_option(media, opt);
    }
    libvlc_media_player_set_media(mp, media);
    if(command_superseded()) return TRUE;
    apply_audio(&options);
    if (libvlc_media_player_play(mp) < 0) return FALSE;
    return TRUE;
}
static DWORD WINAPI control_player(void *unused) {
    char video[24576], audio[16384]; BOOL local;
    BOOL open_failed;
    int command;
    (void)unused;
    for (;;) {
        WaitForSingleObject(changed,INFINITE);
        EnterCriticalSection(&gate);
        command=pending_command; pending_command=0; busy=TRUE;
        preparing=command==CMD_PREPARE || (command==CMD_OPEN && (!mp || core_dirty));
        memcpy(video,next_video,sizeof(video)); memcpy(audio,next_audio,sizeof(audio)); local=next_local;
        LeaveCriticalSection(&gate);
        open_failed=FALSE;
        if (command==CMD_OPEN) open_failed=!open_sync(video,audio,local);
        else if (command==CMD_PREPARE) {
            PlayerOptions options;
            EnterCriticalSection(&gate); options=configured; LeaveCriticalSection(&gate);
            if(!command_superseded()) open_failed=!ensure_core(&options);
        }
        else if (command==CMD_STOP) stop_sync();
        else if (command==CMD_PAUSE && mp) libvlc_media_player_pause(mp);
        else if (command==CMD_CLOSE) {
            stop_sync(); release_core(); return 0;
        }
        EnterCriticalSection(&gate);
        if(command==CMD_OPEN || command==CMD_PREPARE) failed=open_failed;
        if (!pending_command && !shutting_down) apply_audio(&configured);
        busy=preparing=FALSE;
        LeaveCriticalSection(&gate);
    }
}
BOOL player_open(const char *video, const char *audio, BOOL local, WCHAR *err, size_t cap) {
    if (!controller || shutting_down || strlen(video)>=sizeof(next_video) || (audio && strlen(audio)>=sizeof(next_audio))) {
        lstrcpynW(err,L"No se pudo preparar el stream.",(int)cap); return FALSE;
    }
    EnterCriticalSection(&gate);
    strcpy(next_video,video); strcpy(next_audio,audio ? audio : ""); next_local=local;
    failed=FALSE; startup_failed=FALSE; startup_message[0]=0; pending_command=CMD_OPEN;
    LeaveCriticalSection(&gate); SetEvent(changed); return TRUE;
}
static void queue(int command) {
    if (!controller || shutting_down) return;
    EnterCriticalSection(&gate);
    if(command==CMD_PAUSE && (pending_command || busy)) { LeaveCriticalSection(&gate); return; }
    pending_command=command; LeaveCriticalSection(&gate);
    SetEvent(changed);
}
void player_stop(void) { queue(CMD_STOP); }
void player_pause(void) { queue(CMD_PAUSE); }
void player_close(void) {
    if (!controller || shutting_down) return;
    EnterCriticalSection(&gate); shutting_down=TRUE; pending_command=CMD_CLOSE; LeaveCriticalSection(&gate);
    SetEvent(changed);
}
BOOL player_closed(void) {
    if (controller) {
        if(WaitForSingleObject(controller,0)!=WAIT_OBJECT_0) return FALSE;
        CloseHandle(controller); controller=NULL;
        CloseHandle(changed); changed=NULL; DeleteCriticalSection(&gate);
    }
    if (dll) FreeLibrary(dll);
    dll = NULL; startup_failed=preparing=FALSE; startup_message[0]=0;
    return TRUE;
}
int player_state(void) {
    int state;
    if (!controller) return 0;
    EnterCriticalSection(&gate);
    state=busy || pending_command ? 1 : failed ? 7 : mp ? libvlc_media_player_get_state(mp) : 0;
    LeaveCriticalSection(&gate);
    return state;
}
BOOL player_stats(PlayerStats *s) {
    BOOL ok;
    memset(s, 0, sizeof(*s));
    if(!controller) return FALSE;
    EnterCriticalSection(&gate);
    ok=!busy && !pending_command && !shutting_down && media && libvlc_media_get_stats(media,s);
    LeaveCriticalSection(&gate);
    return ok;
}
