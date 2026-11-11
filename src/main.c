#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "engine.h"
#include "player.h"

#define VERSION L"0.5.1"
#define DONE (WM_APP+1)
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
enum { QUERY=100, SEARCH, PASTE, RESULTS, QUALITY, MODE, BRIDGE, PLAY,
       PAUSE, CANCEL, RETRY, OFFLINE };
enum { NONE, DO_SEARCH, DO_PLAY, DO_OFFLINE };
typedef struct {
    int action, height;
    BOOL fallback, ok;
    WCHAR input[2048], error[768];
    EngineResult results[ENGINE_MAX_RESULTS];
    int count;
    EngineStreams streams;
} Work;
static HWND wnd, query, search, paste, list, quality, mode, bridge;
static HWND play, pausebtn, cancelbtn, retry, offline, status, video, labels;
static HFONT font, bold;
static HBRUSH black;
static Engine engine;
static WCHAR base[MAX_PATH], lasturl[2048], lastquery[2048], err[768], engine_error[768];
static EngineResult results[ENGINE_MAX_RESULTS];
static int result_count, last_action, pending, last_state = -1;
static BOOL closing, playing, probed, engine_ok, testmode;
static HANDLE worker, cancel_event;
static DWORD started, test_started, test_ms = 5500;
static Work *task;
static WCHAR test_media[4096], test_report[MAX_PATH];
static PlayerStats final_stats, first_stats;
static BOOL first_frame;
static int exit_code;

static void text(const WCHAR *s) { SetWindowTextW(status, s); }
static int height(void) {
    int i = (int)SendMessageW(quality, CB_GETCURSEL, 0, 0);
    return i == 0 ? 144 : i == 2 ? 360 : 240;
}
static void enable_retry(BOOL on) { EnableWindow(retry, on && last_action != NONE); }
static void fail(const WCHAR *s) {
    playing = FALSE;
    EnableWindow(pausebtn,FALSE);
    text(s); enable_retry(TRUE); EnableWindow(cancelbtn, worker != NULL);
}
static DWORD WINAPI run_work(void *data) {
    Work *w = (Work *)data;
    if (w->action == DO_SEARCH)
        w->ok = engine_search(&engine, w->input, cancel_event, w->results,
                             &w->count, w->error, COUNT(w->error));
    else w->ok = engine_resolve(&engine, w->input, w->height, w->fallback,
                               cancel_event, &w->streams, w->error, COUNT(w->error));
    /* The UI owns w and frees it only after the thread has actually ended. */
    PostMessageW(wnd, DONE, 0, 0);
    return 0;
}
static BOOL utf8(const WCHAR *s, char *out, int cap) {
    return WideCharToMultiByte(CP_UTF8, 0, s, -1, out, cap, NULL, NULL) != 0;
}
static BOOL open_stream(const char *url, const char *audio, BOOL local) {
    if (!player_init(base, video, testmode, err, COUNT(err)) ||
        !player_open(url, audio, local, err, COUNT(err))) {
        fail(err); return FALSE;
    }
    started = GetTickCount(); playing = TRUE; last_state = -1;
    EnableWindow(cancelbtn, TRUE); EnableWindow(pausebtn, FALSE);
    enable_retry(FALSE);
    text(L"Abriendo imagen y audio… búfer de red: 450 ms.");
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
static void launch(int action, BOOL fallback);
static void request(int action) {
    enable_retry(FALSE);
    if (worker) { pending = action; SetEvent(cancel_event); text(L"Cancelando el motor anterior…"); return; }
    launch(action, FALSE);
}
static void launch(int action, BOOL fallback) {
    char url[24576];
    pending = NONE;
    player_stop(); playing = FALSE;
    EnableWindow(pausebtn,FALSE);
    if (!fallback) probed = FALSE;
    if (action == DO_OFFLINE) {
        WCHAR path[MAX_PATH];
        _snwprintf(path, COUNT(path), L"%ls\\media\\prueba-%d.mp4", base, height());
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            fail(L"Falta el clip offline. Extraé TODO el ZIP, incluida la carpeta media."); return;
        }
        utf8(path, url, sizeof(url));
        open_stream(url, NULL, TRUE); return;
    }
    if (action == DO_PLAY && SendMessageW(mode, CB_GETCURSEL, 0, 0) == 1) {
        if (bridge_url(url, sizeof(url))) open_stream(url, NULL, FALSE);
        return;
    }
    if (!engine_ok) { fail(engine_error); return; }
    task = (Work *)calloc(1, sizeof(*task));
    if (!task) { fail(L"No hay memoria para iniciar el motor."); return; }
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
    Work *w;
    if (!worker || WaitForSingleObject(worker, 0) != WAIT_OBJECT_0) return;
    CloseHandle(worker); worker = NULL;
    w = task; task = NULL;
    next = pending; pending = NONE;
    if (closing) { free(w); return; }
    if (WaitForSingleObject(cancel_event, 0) == WAIT_OBJECT_0) {
        free(w);
        if (next) launch(next, FALSE);
        else { text(L"Cancelado. Podés reintentar o cambiar la calidad."); enable_retry(TRUE); EnableWindow(cancelbtn, FALSE); }
        return;
    }
    if (!w->ok) {
        if (w->action == DO_PLAY && !w->fallback && !engine.loader_failed) {
            free(w); probed = TRUE; launch(DO_PLAY, TRUE); return;
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
        enable_retry(TRUE); EnableWindow(cancelbtn, FALSE);
    } else open_stream(w->streams.video, w->streams.audio, FALSE);
    free(w);
}
static void search_query(void) {
    WCHAR input[2048];
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
    int left = w < 750 ? 230 : 280, mainh = h - 190;
#define MOVE(c,x,y,a,b) MoveWindow(c,x,y,a,b,TRUE)
    MOVE(query, 10, 10, w-278, 26); MOVE(search,w-260,10,76,26); MOVE(paste,w-176,10,166,26);
    MOVE(quality,10,46,145,200); MOVE(mode,165,46,146,200); MOVE(bridge,321,46,w-331,24);
    MOVE(list,10,80,left,mainh); MOVE(video,left+20,80,w-left-30,mainh);
    MOVE(play,10,h-78,98,28); MOVE(pausebtn,114,h-78,80,28); MOVE(cancelbtn,200,h-78,86,28);
    MOVE(retry,292,h-78,86,28); MOVE(offline,384,h-78,w-394,28);
    MOVE(status,10,h-44,w-20,38); MOVE(labels,10,h-103,w-20,20);
#undef MOVE
}
static HWND control(const WCHAR *cls, const WCHAR *caption, DWORD style, int id) {
    HWND h = CreateWindowExW(!wcscmp(cls,L"EDIT") || !wcscmp(cls,L"LISTBOX") ? WS_EX_CLIENTEDGE : 0,
        cls, caption, WS_CHILD|WS_VISIBLE|style, 0,0,0,0,wnd,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    return h;
}
static void report_test(BOOL done) {
    FILE *f = _wfopen(test_report, L"wb");
    if (f) {
        fprintf(f,"{\"complete\":%s,\"passed\":%s,\"state\":%d,\"elapsed_ms\":%lu,"
             "\"decoded_video\":%d,\"decoded_audio\":%d,\"displayed_pictures\":%d,"
             "\"played_audio_buffers\":%d,\"read_bytes\":%d,\"first_frame_read_bytes\":%d}\n",
             done ? "true":"false", exit_code ? "false":"true", player_state(),
             (unsigned long)(GetTickCount()-test_started), final_stats.decoded_video,
             final_stats.decoded_audio, final_stats.displayed_pictures, final_stats.played_abuffers,
             final_stats.read_bytes, first_stats.read_bytes);
        fclose(f);
    }
}
static void tick(void) {
    int s;
    finish_work();
    if (closing) { if (!worker && player_closed()) DestroyWindow(wnd); return; }
    if (!playing) {
        if (testmode) { exit_code = 2; report_test(TRUE); PostMessageW(wnd, WM_CLOSE, 0, 0); }
        return;
    }
    s = player_state();
    if (testmode) {
        player_stats(&final_stats);
        if (!first_frame && final_stats.displayed_pictures > 0) { first_stats = final_stats; first_frame = TRUE; }
        if (GetTickCount()-test_started >= test_ms || s == 6 || s == 7) {
            exit_code = !(final_stats.decoded_video > 0 && final_stats.decoded_audio > 0 && final_stats.displayed_pictures > 0);
            report_test(TRUE); PostMessageW(wnd, WM_CLOSE, 0, 0); return;
        }
    }
    if (s == 7 || ((s == 1 || s == 2 || s == 0) && GetTickCount()-started > 25000)) {
        player_stop();
        if (!testmode && last_action == DO_PLAY && !probed && SendMessageW(mode,CB_GETCURSEL,0,0) == 0) {
            probed = TRUE; launch(DO_PLAY,TRUE);
        } else fail(L"No se pudo reproducir. Reintentá, cambiá calidad o usá Puente. YouTube puede exigir verificación; consultá la guía.");
        return;
    }
    if (s != last_state) {
        last_state = s;
        if (s == 3) { EnableWindow(pausebtn,TRUE); text(L"Reproduciendo • H.264 • Cancelar detiene el motor y la reproducción."); }
        else if (s == 4) { EnableWindow(pausebtn,TRUE); text(L"En pausa. Pulsá Pausa para continuar."); }
        else if (s == 6) { playing = FALSE; text(L"Reproducción terminada."); enable_retry(TRUE); EnableWindow(cancelbtn,FALSE); EnableWindow(pausebtn,FALSE); }
    }
}
static LRESULT CALLBACK procedure(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        WCHAR caption[256];
        wnd = h;
        font = CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,DEFAULT_QUALITY,0,L"Tahoma");
        bold = CreateFontW(-14,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,DEFAULT_QUALITY,0,L"Tahoma");
        black = CreateSolidBrush(RGB(0,0,0));
        query=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,QUERY);
        SendMessageW(query,EM_SETLIMITTEXT,2047,0);
        search=control(L"BUTTON",L"Buscar",WS_TABSTOP|BS_DEFPUSHBUTTON,SEARCH);
        paste=control(L"BUTTON",L"Pegar / Reproducir",WS_TABSTOP,PASTE);
        quality=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,QUALITY);
        SendMessageW(quality,CB_ADDSTRING,0,(LPARAM)L"144p · Mínimo");
        SendMessageW(quality,CB_ADDSTRING,0,(LPARAM)L"240p · Liviano");
        SendMessageW(quality,CB_ADDSTRING,0,(LPARAM)L"360p");
        SendMessageW(quality,CB_SETCURSEL,1,0);
        mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,MODE);
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"Directo (experimental)");
        SendMessageW(mode,CB_ADDSTRING,0,(LPARAM)L"Puente en red");
        SendMessageW(mode,CB_SETCURSEL,0,0);
        bridge=control(L"EDIT",L"http://192.168.1.10:8787",ES_AUTOHSCROLL|WS_TABSTOP,BRIDGE);
        EnableWindow(bridge,FALSE); SendMessageW(bridge,EM_SETLIMITTEXT,255,0);
        list=control(L"LISTBOX",L"",LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOTIFY|WS_VSCROLL|WS_TABSTOP,RESULTS);
        SendMessageW(list,LB_SETITEMHEIGHT,0,50);
        video=control(L"STATIC",L"",SS_BLACKRECT,0);
        play=control(L"BUTTON",L"Reproducir",WS_TABSTOP,PLAY);
        pausebtn=control(L"BUTTON",L"Pausa",WS_TABSTOP,PAUSE);
        cancelbtn=control(L"BUTTON",L"Cancelar",WS_TABSTOP,CANCEL);
        retry=control(L"BUTTON",L"Reintentar",WS_TABSTOP,RETRY);
        offline=control(L"BUTTON",L"Probar imagen y audio",WS_TABSTOP,OFFLINE);
        labels=control(L"STATIC",L"",0,0);
        status=control(L"STATIC",L"Escribí un título, pegá un enlace o probá imagen y audio sin Internet.",0,0);
        EnableWindow(cancelbtn,FALSE); EnableWindow(retry,FALSE); EnableWindow(pausebtn,FALSE);
        cancel_event=CreateEventW(NULL,TRUE,FALSE,NULL);
        if (!cancel_event) return -1;
        engine_ok=engine_init(&engine,base,engine_error,COUNT(engine_error));
        _snwprintf(caption,COUNT(caption),L"Windows %lu.%lu • Motor: %ls • VLC 3 x86",
                   (unsigned long)engine.os_major,(unsigned long)engine.os_minor,
                   engine_ok ? (engine.xp ? L"XP" : L"oficial Win10/11") : L"no disponible");
        SetWindowTextW(labels,caption);
        if (!engine_ok) text(engine_error);
        SetTimer(h,1,100,NULL);
        return 0;
    }
    case WM_SIZE: layout(LOWORD(lp),HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x=660; ((MINMAXINFO *)lp)->ptMinTrackSize.y=450; return 0;
    case WM_MEASUREITEM: ((MEASUREITEMSTRUCT *)lp)->itemHeight=50; return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT *)lp;
        RECT r=d->rcItem;
        WCHAR meta[384], duration[32];
        int i=(int)d->itemID;
        BOOL selected=(d->itemState&ODS_SELECTED)!=0;
        if (d->CtlID!=RESULTS || i<0 || i>=result_count) return TRUE;
        FillRect(d->hDC,&r,GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetBkMode(d->hDC,TRANSPARENT);
        SetTextColor(d->hDC,GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        r.left+=7; r.right-=5; r.top+=5; r.bottom=r.top+20;
        SelectObject(d->hDC,bold); DrawTextW(d->hDC,results[i].title,-1,&r,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if (results[i].duration<0) lstrcpyW(duration,L"En vivo / —");
        else _snwprintf(duration,COUNT(duration),L"%d:%02d",results[i].duration/60,results[i].duration%60);
        _snwprintf(meta,COUNT(meta),L"%ls · %ls",results[i].channel,duration);
        r.top+=22; r.bottom=r.top+19;
        SelectObject(d->hDC,font); DrawTextW(d->hDC,meta,-1,&r,DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if (d->itemState&ODS_FOCUS) DrawFocusRect(d->hDC,&d->rcItem);
        return TRUE;
    }
    case WM_COMMAND:
        if (closing) return 0;
        switch (LOWORD(wp)) {
        case SEARCH: search_query(); break;
        case PASTE: paste_play(); break;
        case PLAY: play_selection(); break;
        case RESULTS: if (HIWORD(wp)==LBN_DBLCLK) play_selection(); break;
        case PAUSE: player_pause(); break;
        case CANCEL:
            pending=NONE; SetEvent(cancel_event); player_stop(); playing=FALSE;
            EnableWindow(pausebtn,FALSE);
            text(L"Cancelando…"); enable_retry(TRUE);
            if (!worker) { text(L"Cancelado. Enlace conservado para reintentar."); EnableWindow(cancelbtn,FALSE); }
            break;
        case RETRY: if (last_action) request(last_action); break;
        case OFFLINE: last_action=DO_OFFLINE; request(DO_OFFLINE); break;
        case QUALITY: if (HIWORD(wp)==CBN_SELCHANGE && (last_action==DO_PLAY || last_action==DO_OFFLINE)) request(last_action); break;
        case MODE: if (HIWORD(wp)==CBN_SELCHANGE) EnableWindow(bridge,SendMessageW(mode,CB_GETCURSEL,0,0)==1); break;
        }
        return 0;
    case DONE: finish_work(); return 0;
    case WM_TIMER: tick(); return 0;
    case WM_CLOSE:
        if (!closing) { closing=TRUE; pending=NONE; SetEvent(cancel_event); player_close(); }
        if (!worker && player_closed()) DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h,1); if(cancel_event)CloseHandle(cancel_event);
        DeleteObject(font); DeleteObject(bold); DeleteObject(black);
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
    slash=wcsrchr(base,L'\\'); if (!slash) return 1; *slash=0;
    _snwprintf(test_report,COUNT(test_report),L"%ls\\selftest.json",base);
    argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(i=1;argv && i<argc;i++) {
        if (!wcscmp(argv[i],L"--self-test-media") && i+1<argc) { testmode=TRUE; lstrcpynW(test_media,argv[++i],COUNT(test_media)); }
        else if (!wcscmp(argv[i],L"--report") && i+1<argc) lstrcpynW(test_report,argv[++i],COUNT(test_report));
        else if (!wcscmp(argv[i],L"--test-ms") && i+1<argc) test_ms=(DWORD)_wtoi(argv[++i]);
    }
    if (argv) LocalFree(argv);
    memset(&cls,0,sizeof(cls)); cls.lpfnWndProc=procedure; cls.hInstance=instance;
    cls.lpszClassName=L"TubeLiviano05"; cls.hCursor=LoadCursor(NULL,IDC_ARROW);
    cls.hIcon=LoadIcon(NULL,IDI_APPLICATION); cls.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);
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
        if (msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && msg.hwnd==query) { search_query(); continue; }
        if (IsDialogMessageW(wnd,&msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
