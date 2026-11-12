#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "engine.h"
#include "engine_worker.h"
#include "player.h"
#include "ui.h"
#include "settings.h"
#include "stream_cache.h"

#define VERSION L"0.5.4"
#define DONE (WM_APP+1)
#define FS_ESCAPE 0x5450
#define FS_F11 0x5451
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
enum { NONE, DO_SEARCH, DO_PLAY, DO_OFFLINE };
typedef struct {
    Engine engine; /* Immutable request configuration owned by this worker. */
    int action, height;
    BOOL fallback, ok;
    WCHAR input[2048], error[768];
    EngineResult results[ENGINE_MAX_RESULTS];
    int count;
    EngineStreams streams;
} Work;
static HWND wnd, query, search, paste, list, quality, mode, bridge;
static HWND play, pausebtn, cancelbtn, retry, offline, status, video, labels;
static HWND settingsbtn, homebtn, resultsbtn, fullscreenbtn;
static WINDOWPLACEMENT windowed_placement;
static LONG_PTR windowed_style, windowed_exstyle;
static HWND windowed_focus;
static BOOL escape_hotkey, fullscreen_hotkey, fullscreen_key_held;
static UiStyle style;
static UiControls controls;
static AppSettings prefs;
static WNDPROC edit_procedure;
static BOOL loading_controls;
static Engine engine;
static WCHAR base[MAX_PATH], lasturl[2048], lastquery[2048], err[768], engine_error[768];
static EngineResult results[ENGINE_MAX_RESULTS];
static int result_count, last_action, pending, last_state = -1;
static BOOL closing, playing, probed, engine_ok, testmode, pending_fresh;
static StreamCache stream_cache;
static EngineStreams candidate_streams;
static WCHAR candidate_url[2048];
static BOOL candidate_pending, playback_cached, cached_ready;
static int candidate_height;
static DWORD candidate_resolved;
static int first_frame_ms=-1, first_audio_ms=-1, prepare_call_ms=-1;
static HANDLE worker, cancel_event;
static DWORD started, test_started, test_ms = 5500;
static Work *task;
static WCHAR test_media[4096], test_report[MAX_PATH];
static PlayerStats final_stats, first_stats;
static BOOL first_frame;
static int exit_code;

static void layout(int w, int h);
static void set_fullscreen(BOOL enabled);
static void fullscreen_keys(BOOL enabled);
static void relayout(void) {
    RECT r; if (!wnd) return;
    if(style.fullscreen && !playing) { set_fullscreen(FALSE); return; }
    EnableWindow(fullscreenbtn,playing && !closing);
    fullscreen_keys(GetForegroundWindow()==wnd);
    GetClientRect(wnd,&r); layout(r.right,r.bottom);
}
static void text(const WCHAR *s) { SetWindowTextW(status, s); }
static int height(void) {
    int i = (int)SendMessageW(quality, CB_GETCURSEL, 0, 0);
    return i == 0 ? 144 : i == 2 ? 360 : 240;
}
static void enable_retry(BOOL on) { EnableWindow(retry, on && last_action != NONE); }
static void fail(const WCHAR *s) {
    playing = FALSE; candidate_pending=playback_cached=cached_ready=FALSE;
    relayout();
    EnableWindow(pausebtn,FALSE);
    text(s); enable_retry(TRUE); EnableWindow(cancelbtn, worker != NULL);
}
static DWORD WINAPI run_work(void *data) {
    Work *w = (Work *)data;
    if (w->action == DO_SEARCH)
        w->ok = engine_search(&w->engine, w->input, cancel_event, w->results,
                             &w->count, w->error, COUNT(w->error));
    else w->ok = engine_resolve(&w->engine, w->input, w->height, w->fallback,
                               cancel_event, &w->streams, w->error, COUNT(w->error));
    /* The UI owns w and frees it only after the thread has actually ended. */
    PostMessageW(wnd, DONE, 0, 0);
    return 0;
}
static BOOL utf8(const WCHAR *s, char *out, int cap) {
    return WideCharToMultiByte(CP_UTF8, 0, s, -1, out, cap, NULL, NULL) != 0;
}
static BOOL open_stream(const char *url, const char *audio, BOOL local) {
    DWORD before=GetTickCount();
    if (!player_prepare(base, video, testmode, err, COUNT(err)) ||
        !player_open(url, audio, local, err, COUNT(err))) {
        fail(err); return FALSE;
    }
    prepare_call_ms=(int)(GetTickCount()-before);
    started = GetTickCount(); playing = TRUE; last_state = -1;
    EnableWindow(cancelbtn, TRUE); EnableWindow(pausebtn, FALSE);
    enable_retry(FALSE);
    { WCHAR message[128];
      _snwprintf(message,COUNT(message),L"Abriendo imagen y audio… búfer de red: %d ms.",prefs.cache_ms); text(message); }
    style.page=UI_RESULTS; relayout();
    return TRUE;
}
static BOOL youtube(const WCHAR *s) {
    return wcsncmp(s, L"https://www.youtube.com/", 24) == 0 ||
           wcsncmp(s, L"https://youtube.com/", 20) == 0 ||
           wcsncmp(s, L"https://m.youtube.com/", 22) == 0 ||
           wcsncmp(s, L"https://youtu.be/", 17) == 0 ||
           wcsncmp(s, L"http://www.youtube.com/", 23) == 0 ||
           wcsncmp(s, L"http://youtube.com/", 19) == 0 ||
           wcsncmp(s, L"http://youtu.be/", 16) == 0;
}
static BOOL bridge_url(char *out, size_t cap) {
    WCHAR host[256]; char host8[768], source[6144], encoded[18432];
    WCHAR *token;
    char token8[256] = "";
    size_t n = 0, i;
    GetWindowTextW(bridge, host, COUNT(host));
    token = wcsstr(host, L"?token=");
    if (token) {
        WCHAR *p = token + 7;
        *token = 0;
        if (!*p) { fail(L"El token del puente está vacío."); return FALSE; }
        for (token = p; *token; ++token) {
            if (!( (*token >= L'a' && *token <= L'z') || (*token >= L'A' && *token <= L'Z') ||
                   (*token >= L'0' && *token <= L'9') || wcschr(L"-_.~", *token))) {
                fail(L"Usá un token de letras, números, guiones o puntos en ?token=... ."); return FALSE;
            }
        }
        if (!utf8(p, token8, sizeof(token8))) return FALSE;
    }
    if (wcsncmp(host, L"http://", 7) || wcspbrk(host + 7, L" /?#@\\\t\r\n") || !host[7]) {
        fail(L"Puente: escribí http://IP-DE-LA-PC:8787 (sin barra final). Consultá bridge/README.md.");
        return FALSE;
    }
    if (!utf8(host, host8, sizeof(host8)) || !utf8(lasturl, source, sizeof(source))) return FALSE;
    for (i = 0; source[i]; ++i) {
        unsigned char c = (unsigned char)source[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') encoded[n++] = c;
        else { sprintf(encoded + n, "%%%02X", c); n += 3; }
    }
    encoded[n] = 0;
    return snprintf(out, cap, "%s/play?height=%d&url=%s%s%s", host8, height(), encoded,
                    token8[0] ? "&token=" : "", token8) < (int)cap;
}
static void launch(int action, BOOL fallback, BOOL fresh);
static void request_mode(int action, BOOL fresh) {
    if (closing) return;
    style.page=UI_RESULTS; relayout();
    enable_retry(FALSE);
    if (worker) {
        pending=action; pending_fresh=fresh; SetEvent(cancel_event);
        text(L"Cancelando el motor anterior…"); return;
    }
    launch(action,FALSE,fresh);
}
static void request(int action) { request_mode(action,FALSE); }
static void launch(int action, BOOL fallback, BOOL fresh) {
    char url[24576];
    pending = NONE; pending_fresh=FALSE;
    candidate_pending=playback_cached=cached_ready=FALSE;
    /* A completed bootstrap failure has no media to stop. Keep it idle so
     * prepare can retry a repaired DLL while extraction is still running. */
    if(!player_startup_error(NULL,0)) player_stop();
    playing = FALSE; relayout();
    EnableWindow(pausebtn,FALSE);
    if (!fallback) probed = FALSE;
    if (action == DO_OFFLINE) {
        WCHAR path[MAX_PATH];
        engine_worker_stop();
        int written=_snwprintf(path, COUNT(path), L"%ls\\media\\prueba-%d.mp4", base, height());
        if (written<0 || written>=(int)COUNT(path)) {
            path[0]=0;
            fail(L"La ruta del clip es demasiado larga. Extraiga el ZIP en una carpeta corta."); return;
        }
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            fail(L"Falta el clip offline. Extraé TODO el ZIP, incluida la carpeta media."); return;
        }
        utf8(path, url, sizeof(url));
        open_stream(url, NULL, TRUE); return;
    }
    if (action == DO_PLAY && SendMessageW(mode, CB_GETCURSEL, 0, 0) == 1) {
        engine_worker_stop();
        if (bridge_url(url, sizeof(url))) open_stream(url, NULL, FALSE);
        return;
    }
    if (!engine_ok) { fail(engine_error); return; }
    if(action==DO_PLAY) {
        EngineStreams recent;
        /* An explicit play warms VLC while the extractor works. Search alone
         * never loads the player or starts speculative video requests. */
        if(!player_prepare(base,video,testmode,err,COUNT(err))) { fail(err); return; }
        if(!fallback && !fresh && stream_cache_get(&stream_cache,lasturl,height(),GetTickCount(),&recent)) {
            engine_worker_stop();
            if(open_stream(recent.video,recent.audio,FALSE)) {
                playback_cached=TRUE; text(L"Reabriendo el enlace reciente…");
            }
            return;
        }
    }
    task = (Work *)calloc(1, sizeof(*task));
    if (!task) { fail(L"No hay memoria para iniciar el motor."); return; }
    task->engine = engine;
    task->action = action; task->height = height(); task->fallback = fallback;
    lstrcpynW(task->input, action == DO_SEARCH ? lastquery : lasturl, COUNT(task->input));
    ResetEvent(cancel_event);
    worker = CreateThread(NULL, 0, run_work, task, 0, NULL);
    if (!worker) { free(task); task = NULL; fail(L"Windows no pudo iniciar el hilo del motor."); return; }
    EnableWindow(cancelbtn, TRUE);
    text(action == DO_SEARCH ? L"Buscando… Cancelar libera el motor y sus subprocesos." :
         fallback ? L"Primer intento falló. Verificando formatos compatibles una sola vez…" :
         L"Resolviendo enlace… se abrirá directamente en VLC, sin descargar el video.");
}
static void finish_work(void) {
    int next;
    BOOL next_fresh;
    Work *w;
    if (!worker || WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return;
    CloseHandle(worker); worker = NULL;
    w = task; task = NULL;
    next = pending; next_fresh=pending_fresh; pending = NONE; pending_fresh=FALSE;
    if (closing) { free(w); return; }
    if (WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) {
        free(w);
        if (next) launch(next, FALSE,next_fresh);
        else { text(L"Cancelado. Podés reintentar o cambiar la calidad."); enable_retry(TRUE); EnableWindow(cancelbtn, FALSE); }
        return;
    }
    if (!w->ok) {
        if(w->action==DO_PLAY && player_startup_error(err,COUNT(err))) { fail(err); free(w); return; }
        if (w->action == DO_PLAY && !w->fallback && engine_format_probe_allowed(&w->engine)) {
            free(w); probed = TRUE; launch(DO_PLAY, TRUE,TRUE); return;
        }
        fail(w->error); free(w); return;
    }
    if (w->action == DO_SEARCH) {
        int i;
        result_count = w->count;
        memcpy(results, w->results, sizeof(results));
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (i = 0; i < result_count; ++i) SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)results[i].title);
        if (result_count) SendMessageW(list, LB_SETCURSEL, 0, 0);
        text(result_count ? L"Elegí un resultado y pulsá Reproducir (o hacé doble clic)." : L"Sin resultados. Probá otro título.");
        enable_retry(TRUE); EnableWindow(cancelbtn, !engine_worker_closed());
    } else if(open_stream(w->streams.video,w->streams.audio,FALSE) && !w->fallback) {
        /* Cache only after actual picture/audio proof. Never keep a failed or
         * merely extracted stream, and never renew a cache hit's expiry. */
        candidate_streams=w->streams; candidate_height=w->height;
        lstrcpynW(candidate_url,w->input,COUNT(candidate_url));
        candidate_resolved=GetTickCount(); candidate_pending=TRUE;
    }
    free(w); relayout();
}
static void search_query(void) {
    WCHAR input[2048];
    if (closing) return;
    GetWindowTextW(query, input, COUNT(input));
    if (!input[0]) { text(L"Escribí un título o pegá un enlace de YouTube."); return; }
    if (youtube(input)) { lstrcpynW(lasturl, input, COUNT(lasturl)); last_action = DO_PLAY; request(DO_PLAY); }
    else if (wcsstr(input, L"://")) fail(L"Usá un enlace de YouTube o escribí un título para buscar.");
    else { lstrcpynW(lastquery, input, COUNT(lastquery)); last_action = DO_SEARCH; request(DO_SEARCH); }
}
static void play_selection(void) {
    int i = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (i >= 0 && i < result_count) {
        _snwprintf(lasturl, COUNT(lasturl), L"https://www.youtube.com/watch?v=%ls", results[i].id);
        last_action = DO_PLAY; request(DO_PLAY);
    } else search_query();
}
static void paste_play(void) {
    if (OpenClipboard(wnd)) {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        const WCHAR *s = data ? (const WCHAR *)GlobalLock(data) : NULL;
        if (s) { SetWindowTextW(query, s); GlobalUnlock(data); }
        CloseClipboard();
    }
    search_query();
}
static void layout(int w, int h) {
    ui_layout(wnd,&controls,&style,w,h,prefs.mode==1,playing);
    InvalidateRect(wnd,NULL,FALSE);
}
static HWND control(const WCHAR *cls, const WCHAR *caption, DWORD flags, int id) {
    if (!wcscmp(cls,L"BUTTON")) flags=(flags&~BS_TYPEMASK)|BS_OWNERDRAW;
    return CreateWindowExW(0,cls,caption,WS_CHILD|WS_VISIBLE|flags,0,0,0,0,
                          wnd,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
}
/* Keep VLC attached to the same native video child: entering/leaving only
 * changes window geometry, without stopping or resolving the stream again. */
static void fullscreen_keys(BOOL enabled) {
    BOOL escape=enabled && style.fullscreen && !closing;
    BOOL f11=enabled && playing && !closing;
    /* VLC's child may have another thread's focus. Scope system hotkeys to our
     * active player window, releasing them whenever another app is active. */
    if(escape && !escape_hotkey) escape_hotkey=RegisterHotKey(wnd,FS_ESCAPE,0,VK_ESCAPE);
    else if(!escape && escape_hotkey) { UnregisterHotKey(wnd,FS_ESCAPE); escape_hotkey=FALSE; }
    if(f11 && !fullscreen_hotkey) fullscreen_hotkey=RegisterHotKey(wnd,FS_F11,0,VK_F11);
    else if(!f11 && fullscreen_hotkey) { UnregisterHotKey(wnd,FS_F11); fullscreen_hotkey=FALSE; }
}
static BOOL fullscreen_bounds(void) {
    MONITORINFO monitor;
    ZeroMemory(&monitor,sizeof(monitor)); monitor.cbSize=sizeof(monitor);
    if(!GetMonitorInfoW(MonitorFromWindow(wnd,MONITOR_DEFAULTTONEAREST),&monitor)) return FALSE;
    return SetWindowPos(wnd,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,
        monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,
        SWP_FRAMECHANGED|SWP_NOOWNERZORDER);
}
static void set_fullscreen(BOOL enabled) {
    if(enabled==style.fullscreen) return;
    if(enabled) {
        if(closing || !playing) return;
        ZeroMemory(&windowed_placement,sizeof(windowed_placement));
        windowed_placement.length=sizeof(windowed_placement);
        if(!GetWindowPlacement(wnd,&windowed_placement)) return;
        windowed_style=GetWindowLongPtrW(wnd,GWL_STYLE);
        windowed_exstyle=GetWindowLongPtrW(wnd,GWL_EXSTYLE);
        windowed_focus=GetFocus(); style.page=UI_RESULTS; style.fullscreen=TRUE;
        SetWindowLongPtrW(wnd,GWL_STYLE,
            (windowed_style&~(WS_OVERLAPPEDWINDOW|WS_MAXIMIZE|WS_MINIMIZE))|WS_POPUP);
        SetWindowLongPtrW(wnd,GWL_EXSTYLE,windowed_exstyle&~(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE));
        if(!fullscreen_bounds()) {
            set_fullscreen(FALSE); text(L"No se pudo abrir pantalla completa."); return;
        }
        SetWindowTextW(fullscreenbtn,L"Volver a ventana · Esc / F11");
        fullscreen_keys(GetForegroundWindow()==wnd);
        relayout(); SetFocus(fullscreenbtn);
    } else {
        style.fullscreen=FALSE; fullscreen_keys(GetForegroundWindow()==wnd);
        SetWindowLongPtrW(wnd,GWL_STYLE,windowed_style&~(WS_MAXIMIZE|WS_MINIMIZE));
        SetWindowLongPtrW(wnd,GWL_EXSTYLE,windowed_exstyle);
        SetWindowPlacement(wnd,&windowed_placement);
        SetWindowPos(wnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_FRAMECHANGED);
        SetWindowTextW(fullscreenbtn,L"Pantalla completa · F11");
        relayout();
        if(IsWindow(windowed_focus) && IsWindowVisible(windowed_focus) && IsWindowEnabled(windowed_focus))
            SetFocus(windowed_focus);
        else SetFocus(fullscreenbtn);
    }
}
static void apply_options(void) {
    PlayerOptions options={prefs.cache_ms,prefs.prefetch_kib,prefs.decoder_threads,prefs.volume,prefs.muted};
    player_configure(&options);
    engine.search_limit=prefs.results_limit;
    engine.search_timeout_ms=prefs.search_timeout_s*1000;
    engine.resolve_timeout_ms=prefs.resolve_timeout_s*1000;
}
static void remember_window(void) {
    WINDOWPLACEMENT pos;
    ZeroMemory(&pos,sizeof(pos)); pos.length=sizeof(pos);
    if(style.fullscreen) pos=windowed_placement;
    if(style.fullscreen || GetWindowPlacement(wnd,&pos)) {
        prefs.width=pos.rcNormalPosition.right-pos.rcNormalPosition.left;
        prefs.height=pos.rcNormalPosition.bottom-pos.rcNormalPosition.top;
        settings_validate(&prefs);
    }
}
static void save_preferences(void) {
    WCHAR notice[256];
    if(!testmode) {
        GetWindowTextW(bridge,prefs.bridge_url,COUNT(prefs.bridge_url));
        settings_save(&prefs,base,notice,COUNT(notice));
    }
}
static void configure(void) {
    AppSettings previous;
    WCHAR notice[512];
    set_fullscreen(FALSE);
    GetWindowTextW(bridge,prefs.bridge_url,COUNT(prefs.bridge_url));
    previous=prefs;
    if(!settings_dialog(wnd,&prefs,base,notice,COUNT(notice))) return;
    loading_controls=TRUE;
    SendMessageW(quality,CB_SETCURSEL,prefs.quality==144 ? 0 : prefs.quality==360 ? 2 : 1,0);
    SendMessageW(mode,CB_SETCURSEL,prefs.mode,0);
    SetWindowTextW(bridge,prefs.bridge_url);
    EnableWindow(bridge,prefs.mode==1);
    loading_controls=FALSE;
    if(style.dark!=prefs.dark_theme || style.large!=prefs.large_text) {
        UiStyle old=style;
        ui_style_init(&style,prefs.dark_theme,prefs.large_text); style.page=old.page;
        ui_set_fonts(&style,&controls);
        ui_style_free(&old);
    }
    apply_options(); relayout();
    if((previous.quality!=prefs.quality || previous.mode!=prefs.mode ||
        wcscmp(previous.bridge_url,prefs.bridge_url)) && style.page==UI_RESULTS &&
       (last_action==DO_PLAY || last_action==DO_OFFLINE) && (playing || worker)) request(last_action);
    if(notice[0]) text(notice);
    else text(L"Configuración aplicada. Búfer e hilos se aplican al abrir el próximo video.");
}
/* XP has no EM_SETCUEBANNER. Paint a hint over the real, empty native edit;
 * keyboard selection, clipboard, accessibility name and undo stay native. */
static LRESULT CALLBACK query_procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    LRESULT result=CallWindowProcW(edit_procedure,h,msg,wp,lp);
    if(msg==WM_PAINT && GetWindowTextLengthW(h)==0) {
        HDC dc=GetDC(h); RECT r; HGDIOBJ old;
        GetClientRect(h,&r); r.left+=10; r.right-=10;
        old=SelectObject(dc,style.normal); SetTextColor(dc,style.muted); SetBkMode(dc,TRANSPARENT);
        DrawTextW(dc,L"Buscar videos o pegar enlace…",-1,&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        SelectObject(dc,old); ReleaseDC(h,dc);
    } else if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS) {
        InvalidateRect(h,NULL,FALSE); InvalidateRect(wnd,NULL,FALSE);
    } else if(msg==WM_SETTEXT || msg==WM_CHAR || msg==WM_PASTE || msg==WM_CUT || msg==WM_UNDO)
        InvalidateRect(h,NULL,FALSE);
    return result;
}
static BOOL report_test(BOOL done) {
    FILE *f = _wfopen(test_report, L"wb");
    BOOL ok;
    int written;
    if (!f) return FALSE;
        written=fprintf(f,"{\"version\":\"%ls\",\"frontend_bits\":%u,\"windows_major\":%lu,\"windows_minor\":%lu,"
             "\"selected_engine\":\"%s\",\"complete\":%s,\"passed\":%s,\"state\":%d,\"elapsed_ms\":%lu,"
             "\"decoded_video\":%d,\"decoded_audio\":%d,\"displayed_pictures\":%d,"
             "\"played_audio_buffers\":%d,\"read_bytes\":%d,\"first_frame_read_bytes\":%d,"
             "\"first_frame_stats_ms\":%d,\"first_audio_stats_ms\":%d,\"prepare_call_ms\":%d}\n",
             VERSION,(unsigned)(sizeof(void *)*8),(unsigned long)engine.os_major,(unsigned long)engine.os_minor,
             engine_ok ? (engine.xp ? "xp" : "modern") : "unavailable",
             done ? "true":"false", exit_code ? "false":"true", player_state(),
             (unsigned long)(GetTickCount()-test_started), final_stats.decoded_video,
             final_stats.decoded_audio, final_stats.displayed_pictures, final_stats.played_abuffers,
             final_stats.read_bytes, first_stats.read_bytes,first_frame_ms,first_audio_ms,prepare_call_ms);
    ok=written>0;
    if (fclose(f)!=0) ok=FALSE;
    return ok;
}
static void tick(void) {
    int s;
    if(!(GetAsyncKeyState(VK_F11)&0x8000)) fullscreen_key_held=FALSE;
    finish_work();
    if (closing) { if (!worker && player_closed() && engine_worker_closed()) DestroyWindow(wnd); return; }
    if (!playing) {
        if(!worker) EnableWindow(cancelbtn,!engine_worker_closed());
        if (testmode) { exit_code = 2; if(!report_test(TRUE))exit_code=3; PostMessageW(wnd, WM_CLOSE, 0, 0); }
        return;
    }
    s = player_state();
    if(player_preparing()) started=GetTickCount();
    if((candidate_pending || (playback_cached && !cached_ready)) && s!=7) {
        PlayerStats proof;
        if(player_stats(&proof) && proof.displayed_pictures>0) {
            /* Visible cached video is progress even if its audio slave is late. */
            if(candidate_pending && proof.played_abuffers>0) {
                stream_cache_put(&stream_cache,candidate_url,candidate_height,candidate_resolved,&candidate_streams);
                candidate_pending=FALSE;
            }
            if(playback_cached) cached_ready=TRUE;
        }
    }
    if (testmode) {
        player_stats(&final_stats);
        if (!first_frame && final_stats.displayed_pictures > 0) {
            first_stats = final_stats; first_frame = TRUE;
            first_frame_ms=(int)(GetTickCount()-test_started);
        }
        if(first_audio_ms<0 && final_stats.played_abuffers>0) first_audio_ms=(int)(GetTickCount()-test_started);
        if (GetTickCount()-test_started >= test_ms || s == 6 || s == 7) {
            exit_code = !(s!=7 && final_stats.decoded_video > 0 && final_stats.decoded_audio > 0 &&
                          final_stats.displayed_pictures > 0 && final_stats.played_abuffers > 0);
            if (!report_test(TRUE)) exit_code=3;
            PostMessageW(wnd, WM_CLOSE, 0, 0); return;
        }
    }
    if (s == 7 || (playback_cached && !cached_ready && GetTickCount()-started>6000) ||
        ((s == 1 || s == 2 || s == 0) && GetTickCount()-started > 25000)) {
        BOOL startup_problem=player_startup_error(err,COUNT(err));
        player_stop(); candidate_pending=FALSE;
        if(startup_problem) { fail(err); return; }
        if(!testmode && playback_cached && last_action==DO_PLAY) {
            stream_cache_remove(&stream_cache,lasturl,height()); playback_cached=FALSE;
            launch(DO_PLAY,FALSE,TRUE); return;
        }
        if (!testmode && last_action == DO_PLAY && !probed && SendMessageW(mode,CB_GETCURSEL,0,0) == 0) {
            probed = TRUE; launch(DO_PLAY,TRUE,TRUE);
        } else fail(L"No se pudo reproducir. Reintentá, cambiá calidad o usá Puente. YouTube puede exigir verificación; consultá la guía.");
        return;
    }
    if (s != last_state) {
        last_state = s;
        if (s == 3) { EnableWindow(pausebtn,TRUE); text(L"Reproduciendo • H.264 • Cancelar detiene el motor y la reproducción."); }
        else if (s == 4) { EnableWindow(pausebtn,TRUE); text(L"En pausa. Pulsá Pausa para continuar."); }
        else if (s == 6) { playing = FALSE; relayout(); text(L"Reproducción terminada."); enable_retry(TRUE); EnableWindow(cancelbtn,FALSE); EnableWindow(pausebtn,FALSE); }
    }
}
static LRESULT CALLBACK procedure(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        WCHAR caption[256];
        wnd = h;
        ui_style_init(&style,prefs.dark_theme,prefs.large_text);
        loading_controls=TRUE;
        query=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,QUERY);
        SendMessageW(query,EM_SETLIMITTEXT,2047,0);
        edit_procedure=(WNDPROC)SetWindowLongPtrW(query,GWLP_WNDPROC,(LONG_PTR)query_procedure);
        search=control(L"BUTTON",L"Buscar",WS_TABSTOP|BS_DEFPUSHBUTTON,SEARCH);
        paste=control(L"BUTTON",L"Pegar / Reproducir",WS_TABSTOP,PASTE);
        quality=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,QUALITY);
        SendMessageW(quality,CB_ADDSTRING,0,(LPARAM)L"144p · Mínimo");
        SendMessageW(quality,CB_ADDSTRING,0,(LPARAM)L"240p · Liviano");
        SendMessageW(quality,CB_ADDSTRING,0,(LPARAM)L"360p");
        SendMessageW(quality,CB_SETCURSEL,prefs.quality==144 ? 0 : prefs.quality==360 ? 2 : 1,0);
        mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,MODE);
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"Directo");
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"Puente en red");
        SendMessageW(mode,CB_SETCURSEL,prefs.mode,0);
        bridge=control(L"EDIT",prefs.bridge_url,ES_AUTOHSCROLL|WS_TABSTOP,BRIDGE);
        EnableWindow(bridge,prefs.mode==1); SendMessageW(bridge,EM_SETLIMITTEXT,255,0);
        list=control(L"LISTBOX",L"",LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT|LBS_HASSTRINGS|LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,RESULTS);
        SendMessageW(list,LB_SETITEMHEIGHT,0,50);
        video=control(L"STATIC",L"",SS_BLACKRECT,0);
        play=control(L"BUTTON",L"Reproducir",WS_TABSTOP,PLAY);
        pausebtn=control(L"BUTTON",L"Pausa",WS_TABSTOP,PAUSE);
        cancelbtn=control(L"BUTTON",L"Cancelar",WS_TABSTOP,CANCEL);
        retry=control(L"BUTTON",L"Reintentar",WS_TABSTOP,RETRY);
        offline=control(L"BUTTON",L"Probar imagen y audio",WS_TABSTOP,OFFLINE);
        settingsbtn=control(L"BUTTON",L"Configuración",WS_TABSTOP,SETTINGS);
        homebtn=control(L"BUTTON",L"Inicio",WS_TABSTOP,HOME);
        resultsbtn=control(L"BUTTON",L"Resultados",WS_TABSTOP,RESULTS_NAV);
        fullscreenbtn=control(L"BUTTON",L"Pantalla completa · F11",WS_TABSTOP,FULLSCREEN);
        EnableWindow(fullscreenbtn,FALSE);
        controls=(UiControls){query,search,paste,list,quality,mode,bridge,play,pausebtn,cancelbtn,retry,offline,NULL,NULL,video,settingsbtn,homebtn,resultsbtn,fullscreenbtn};
        labels=control(L"STATIC",L"",0,0);
        status=control(L"STATIC",L"Escribí un título, pegá un enlace o probá imagen y audio sin Internet.",0,0);
        controls.status=status; controls.labels=labels;
        ui_set_fonts(&style,&controls); loading_controls=FALSE;
        EnableWindow(cancelbtn,FALSE); EnableWindow(retry,FALSE); EnableWindow(pausebtn,FALSE);
        cancel_event=CreateEventW(NULL,TRUE,FALSE,NULL);
        if (!cancel_event) return -1;
        stream_cache_init(&stream_cache);
        engine_ok=engine_init(&engine,base,engine_error,COUNT(engine_error));
        apply_options();
        _snwprintf(caption,COUNT(caption),L"Windows %lu.%lu • Motor: %ls • VLC 3 x86",
                   (unsigned long)engine.os_major,(unsigned long)engine.os_minor,
                   engine_ok ? (engine.xp ? L"XP" : L"oficial Win10/11") : L"no disponible");
        SetWindowTextW(labels,caption);
        if (!engine_ok) text(engine_error);
        SetTimer(h,1,100,NULL);
        return 0;
    }
    case WM_SIZE: layout(LOWORD(lp),HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: {
        RECT minimum={0,0,660,450};
        if(style.fullscreen) return 0;
        AdjustWindowRectEx(&minimum,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,FALSE,0);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x=minimum.right-minimum.left;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y=minimum.bottom-minimum.top;
        return 0;
    }
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lp)->itemHeight=50; return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT paint; HDC dc=BeginPaint(h,&paint);
        ui_paint(h,dc,&controls,&style,result_count>0,playing); EndPaint(h,&paint); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN:
        return (LRESULT)ui_control_color(&style,msg,(HDC)wp,(HWND)lp);
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT *)lp;
        if(!ui_draw_button(&style,d)) ui_draw_result(&style,d,results,result_count);
        return TRUE;
    }
    case WM_ACTIVATE:
        fullscreen_keys(LOWORD(wp)!=WA_INACTIVE);
        return 0;
    case WM_DISPLAYCHANGE: if(style.fullscreen) { fullscreen_bounds(); relayout(); } return 0;
    case WM_HOTKEY:
        if(!closing) {
            if(wp==FS_ESCAPE && style.fullscreen) set_fullscreen(FALSE);
            else if(wp==FS_F11 && playing && !fullscreen_key_held) {
                fullscreen_key_held=TRUE; set_fullscreen(!style.fullscreen);
            }
        }
        return 0;
    case WM_EXITSIZEMOVE: if(!testmode) { remember_window(); if(prefs.remember_size) save_preferences(); } return 0;
    case WM_COMMAND:
        if (closing || loading_controls) return 0;
        switch (LOWORD(wp)) {
        case SEARCH: search_query(); break;
        case PASTE: paste_play(); break;
        case PLAY: play_selection(); break;
        case RESULTS: if (HIWORD(wp)==LBN_DBLCLK) play_selection(); break;
        case PAUSE: player_pause(); break;
        case CANCEL:
            pending=NONE; candidate_pending=playback_cached=cached_ready=FALSE; SetEvent(cancel_event); engine_worker_stop(); player_stop(); playing=FALSE; relayout();
            EnableWindow(pausebtn,FALSE);
            text(L"Cancelando…"); enable_retry(TRUE);
            if (!worker) { text(L"Cancelado. Enlace conservado para reintentar."); EnableWindow(cancelbtn,FALSE); }
            break;
        case RETRY: if (last_action) request_mode(last_action,TRUE); break;
        case OFFLINE: last_action=DO_OFFLINE; request(DO_OFFLINE); break;
        case QUALITY: if(HIWORD(wp)==CBN_SELCHANGE) {
            prefs.quality=height(); save_preferences();
            if(style.page==UI_RESULTS && (last_action==DO_PLAY || last_action==DO_OFFLINE)) request(last_action);
            } break;
        case MODE: if(HIWORD(wp)==CBN_SELCHANGE) {
            prefs.mode=(int)SendMessageW(mode,CB_GETCURSEL,0,0); EnableWindow(bridge,prefs.mode==1);
            save_preferences(); relayout();
            } break;
        case BRIDGE: if(HIWORD(wp)==EN_KILLFOCUS) {
            GetWindowTextW(bridge,prefs.bridge_url,COUNT(prefs.bridge_url)); save_preferences();
            } break;
        case SETTINGS: configure(); break;
        case FULLSCREEN: set_fullscreen(!style.fullscreen); break;
        case HOME:
            pending=NONE; candidate_pending=playback_cached=cached_ready=FALSE; SetEvent(cancel_event); engine_worker_stop(); player_stop(); playing=FALSE;
            EnableWindow(pausebtn,FALSE); EnableWindow(cancelbtn,worker!=NULL);
            style.page=UI_HOME; relayout(); SetFocus(query);
            text(L"Escribí un título o pegá un enlace. Tus resultados siguen en Resultados."); break;
        case RESULTS_NAV: style.page=UI_RESULTS; relayout(); break;
        }
        return 0;
    case DONE: finish_work(); return 0;
    case WM_TIMER: tick(); return 0;
    case WM_CLOSE:
        if (!closing) { if(!testmode) { remember_window(); save_preferences(); } closing=TRUE; pending=NONE; SetEvent(cancel_event); engine_worker_stop(); player_close(); }
        if (!worker && player_closed() && engine_worker_closed()) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        fullscreen_keys(FALSE);
        KillTimer(h,1); if(cancel_event)CloseHandle(cancel_event);
        ui_style_free(&style);
        PostQuitMessage(exit_code); return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,LPSTR command,int show) {
    WNDCLASSW cls;
    MSG msg;
    int argc,i;
    WCHAR **argv;
    WCHAR *slash;
    RECT workarea;
    int width=840, window_height=590;
    (void)previous; (void)command;
    /* Child loader errors must return a status to the GUI, never a modal system
     * dialog that leaves extraction stuck until the user clicks through it. */
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    {
        DWORD length=GetModuleFileNameW(NULL,base,COUNT(base));
        if (!length || length>=COUNT(base)) {
            MessageBoxW(NULL,L"Extraé TubeLiviano en una ruta más corta, por ejemplo E:\\TubeLiviano.",
                        L"TubeLiviano " VERSION,MB_OK|MB_ICONERROR);
            return 1;
        }
    }
    slash=wcsrchr(base,L'\\'); if (!slash) return 1;
    if (slash==base+2 && base[1]==L':') slash[1]=0; else *slash=0;
    _snwprintf(test_report,COUNT(test_report),L"%ls\\selftest.json",base);
    argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(i=1;argv && i<argc;i++) {
        if (!wcscmp(argv[i],L"--self-test-media") && i+1<argc) { testmode=TRUE; lstrcpynW(test_media,argv[++i],COUNT(test_media)); }
        else if (!wcscmp(argv[i],L"--report") && i+1<argc) lstrcpynW(test_report,argv[++i],COUNT(test_report));
        else if (!wcscmp(argv[i],L"--test-ms") && i+1<argc) test_ms=(DWORD)_wtoi(argv[++i]);
    }
    if (argv) LocalFree(argv);
    settings_defaults(&prefs);
    if(!testmode) { WCHAR notice[256]; settings_load(&prefs,base,notice,COUNT(notice)); }
    if(prefs.remember_size) { width=prefs.width; window_height=prefs.height; }
    memset(&cls,0,sizeof(cls)); cls.lpfnWndProc=procedure; cls.hInstance=instance;
    cls.lpszClassName=L"TubeLiviano05"; cls.hCursor=LoadCursor(NULL,IDC_ARROW);
    cls.hIcon=LoadIcon(NULL,IDI_APPLICATION); cls.hbrBackground=NULL;
    if (!RegisterClassW(&cls)) return 1;
    if (SystemParametersInfoW(SPI_GETWORKAREA,0,&workarea,0)) {
        if (workarea.right-workarea.left<width) width=workarea.right-workarea.left;
        if (workarea.bottom-workarea.top<window_height) window_height=workarea.bottom-workarea.top;
    }
    wnd=CreateWindowW(cls.lpszClassName,L"TubeLiviano " VERSION L" · x86",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
         CW_USEDEFAULT,CW_USEDEFAULT,width,window_height,NULL,NULL,instance,NULL);
    if (!wnd) return 1;
    ShowWindow(wnd,show); UpdateWindow(wnd);
    if (testmode) {
        char path[16384];
        test_started=GetTickCount();
        if (utf8(test_media,path,sizeof(path))) open_stream(path,NULL,strncmp(path,"http",4)!=0);
    }
    SetFocus(query);
    while (GetMessageW(&msg,NULL,0,0)>0) {
        if(msg.message==WM_KEYDOWN && !closing && (GetKeyState(VK_CONTROL)&0x8000)) {
            if(msg.wParam=='F') { set_fullscreen(FALSE); SetFocus(query); SendMessageW(query,EM_SETSEL,0,-1); continue; }
            if(msg.wParam==VK_OEM_COMMA) { configure(); continue; }
        }
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_F11 && !closing) {
            if(!(msg.lParam&(1L<<30))) set_fullscreen(!style.fullscreen);
            continue;
        }
        /* Releasing fullscreen must not turn the same held Esc into Cancel. */
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && (msg.lParam&(1L<<30))) continue;
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && style.fullscreen && !closing) {
            set_fullscreen(FALSE); continue;
        }
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && !closing &&
           !SendMessageW(quality,CB_GETDROPPEDSTATE,0,0) && !SendMessageW(mode,CB_GETDROPPEDSTATE,0,0)) {
            SendMessageW(wnd,WM_COMMAND,CANCEL,0); continue;
        }
        if (msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && msg.hwnd==query) { if(!closing)search_query(); continue; }
        if (IsDialogMessageW(wnd,&msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
