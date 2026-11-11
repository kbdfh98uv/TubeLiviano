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
static BOOL busy, shutting_down, failed;
static char next_video[24576], next_audio[16384];
static BOOL next_local;
enum { CMD_OPEN=1, CMD_STOP, CMD_PAUSE, CMD_CLOSE };
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
FN(const char *, libvlc_errmsg, (void));

static void error(WCHAR *out, size_t cap, const WCHAR *fallback) {
    const char *s = libvlc_errmsg ? libvlc_errmsg() : NULL;
    if (!s || !MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap)) {
        lstrcpynW(out, fallback, (int)cap);
    }
}

BOOL player_init(const WCHAR *base, HWND video, BOOL test, WCHAR *err, size_t cap) {
    WCHAR folder[MAX_PATH], path[MAX_PATH];
    WCHAR certfolder[MAX_PATH];
    char trust[1024];
    const char *args[] = {"--ignore-config", "--no-media-library", "--no-video-title-show",
        "--no-osd", "--no-sub-autodetect-file", "--avcodec-threads=1",
        "--avcodec-hw=none", "--network-caching=450", "--file-caching=150",
        "--vout=wingdi", "--stats", "--prefetch-buffer-size=128",
        "--prefetch-read-size=16384", "--prefetch-seek-threshold=32768", trust, "--aout=dummy"};
    if (vlc) return TRUE;
    /* A failed initialization may be retried after fixing the portable files. */
    if (dll) { FreeLibrary(dll); dll=NULL; libvlc_errmsg=NULL; }
    target = video;
    if (wcslen(base) + 25 >= MAX_PATH) {
        lstrcpynW(err, L"Extraé el ZIP en una ruta más corta (menos de 230 caracteres).", (int)cap);
        return FALSE;
    }
    _snwprintf(folder, MAX_PATH, L"%ls\\vlc", base);
    _snwprintf(certfolder, MAX_PATH, L"%ls\\certs", folder);
    strcpy(trust,"--gnutls-dir-trust=");
    if (!WideCharToMultiByte(CP_UTF8,0,certfolder,-1,trust+19,sizeof(trust)-19,NULL,NULL)) {
        lstrcpynW(err,L"No se pudo configurar la carpeta de certificados.",(int)cap); return FALSE;
    }
    _snwprintf(path, MAX_PATH, L"%ls\\libvlc.dll", folder);
    SetDllDirectoryW(folder);
    dll = LoadLibraryW(path);
    if (!dll) {
        lstrcpynW(err, L"No se pudo cargar vlc\\libvlc.dll x86. Extraé TODO el ZIP.", (int)cap);
        return FALSE;
    }
#define LOAD(name) do { FARPROC p = GetProcAddress(dll, #name); memcpy(&name, &p, sizeof(name)); if (!name) goto missing; } while (0)
    LOAD(libvlc_new); LOAD(libvlc_release); LOAD(libvlc_media_player_new);
    LOAD(libvlc_media_player_release); LOAD(libvlc_media_player_set_hwnd);
    LOAD(libvlc_media_new_location); LOAD(libvlc_media_new_path); LOAD(libvlc_media_add_option);
    LOAD(libvlc_media_release); LOAD(libvlc_media_player_set_media); LOAD(libvlc_media_player_play);
    LOAD(libvlc_media_player_stop); LOAD(libvlc_media_player_pause);
    LOAD(libvlc_media_player_get_state); LOAD(libvlc_media_get_stats); LOAD(libvlc_errmsg);
    /* Test mode uses a dummy audio sink on headless Wine; decoding is still checked. */
    vlc = libvlc_new((int)(sizeof(args)/sizeof(args[0])) - (test ? 0 : 1), args);
    if (!vlc) { error(err, cap, L"VLC no pudo inicializar sus plugins."); return FALSE; }
    mp = libvlc_media_player_new(vlc);
    if (!mp) { error(err, cap, L"VLC no pudo crear el reproductor."); libvlc_release(vlc); vlc=NULL; return FALSE; }
    libvlc_media_player_set_hwnd(mp, target);
    InitializeCriticalSection(&gate);
    changed = CreateEventW(NULL,FALSE,FALSE,NULL);
    if (changed) controller = CreateThread(NULL,0,control_player,NULL,0,NULL);
    if (!controller) {
        if(changed) CloseHandle(changed);
        changed=NULL; DeleteCriticalSection(&gate);
        libvlc_media_player_release(mp); libvlc_release(vlc); mp=vlc=NULL;
        lstrcpynW(err,L"No se pudo crear el hilo de reproducción.",(int)cap); return FALSE;
    }
    return TRUE;
missing:
    lstrcpynW(err, L"La biblioteca VLC no tiene la API de VLC 3 esperada.", (int)cap);
    FreeLibrary(dll); dll = NULL;
    return FALSE;
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
    stop_sync();
    newmedia = local ? libvlc_media_new_path(vlc, video) : libvlc_media_new_location(vlc, video);
    if (!newmedia) return FALSE;
    EnterCriticalSection(&gate);
    media = newmedia;
    LeaveCriticalSection(&gate);
    libvlc_media_add_option(media, ":network-caching=450");
    /* set_hwnd resets VLC 3's inherited vout/hw options; set per-media afterwards. */
    libvlc_media_add_option(media, ":vout=wingdi");
    libvlc_media_add_option(media, ":avcodec-hw=none");
    if (audio && *audio) {
        snprintf(opt, sizeof(opt), ":input-slave=%s", audio);
        libvlc_media_add_option(media, opt);
    }
    libvlc_media_player_set_media(mp, media);
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
        memcpy(video,next_video,sizeof(video)); memcpy(audio,next_audio,sizeof(audio)); local=next_local;
        LeaveCriticalSection(&gate);
        open_failed=FALSE;
        if (command==CMD_OPEN) open_failed=!open_sync(video,audio,local);
        else if (command==CMD_STOP) stop_sync();
        else if (command==CMD_PAUSE) libvlc_media_player_pause(mp);
        else if (command==CMD_CLOSE) {
            stop_sync(); libvlc_media_player_release(mp); libvlc_release(vlc);
            mp=vlc=NULL; return 0;
        }
        EnterCriticalSection(&gate); if(command==CMD_OPEN) failed=open_failed; busy=FALSE; LeaveCriticalSection(&gate);
    }
}
BOOL player_open(const char *video, const char *audio, BOOL local, WCHAR *err, size_t cap) {
    if (!controller || shutting_down || strlen(video)>=sizeof(next_video) || (audio && strlen(audio)>=sizeof(next_audio))) {
        lstrcpynW(err,L"No se pudo preparar el stream.",(int)cap); return FALSE;
    }
    EnterCriticalSection(&gate);
    strcpy(next_video,video); strcpy(next_audio,audio ? audio : ""); next_local=local;
    failed=FALSE; pending_command=CMD_OPEN;
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
    dll = NULL;
    SetDllDirectoryW(L"");
    return TRUE;
}
int player_state(void) {
    BOOL opening, error_state;
    if (!controller) return 0;
    EnterCriticalSection(&gate); opening=busy || pending_command; error_state=failed; LeaveCriticalSection(&gate);
    return opening ? 1 : error_state ? 7 : mp ? libvlc_media_player_get_state(mp) : 0;
}
BOOL player_stats(PlayerStats *s) {
    BOOL ok;
    memset(s, 0, sizeof(*s));
    if(!controller) return FALSE;
    EnterCriticalSection(&gate); ok=media && libvlc_media_get_stats(media,s); LeaveCriticalSection(&gate);
    return ok;
}
