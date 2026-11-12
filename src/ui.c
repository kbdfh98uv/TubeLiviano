#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include "ui.h"

static int smaller(int a, int b) { return a < b ? a : b; }
static int larger(int a, int b) { return a > b ? a : b; }
static void fill(HDC dc, const RECT *r, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH));
}
static void round_box(HDC dc, const RECT *r, int radius, COLORREF bg, COLORREF edge) {
    HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, bg); SetDCPenColor(dc, edge);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius, radius);
    SelectObject(dc, brush); SelectObject(dc, pen);
}
static void line(HDC dc, int x, int y, int x2, int y2, COLORREF color) {
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, color); MoveToEx(dc, x, y, NULL); LineTo(dc, x2, y2);
    SelectObject(dc, pen);
}
static void label(HDC dc, const WCHAR *text, RECT r, HFONT font, COLORREF color, UINT flags) {
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
static void logo(HDC dc, int x, int y, int w, int h, COLORREF accent) {
    RECT r = {x, y, x+w, y+h}; POINT p[3];
    HGDIOBJ brush, pen;
    round_box(dc, &r, h/3, accent, accent);
    p[0].x=x+w*42/100; p[0].y=y+h*26/100;
    p[1].x=x+w*42/100; p[1].y=y+h*74/100;
    p[2].x=x+w*67/100; p[2].y=y+h/2;
    brush=SelectObject(dc, GetStockObject(WHITE_BRUSH));
    pen=SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc,p,3); SelectObject(dc,brush); SelectObject(dc,pen);
}
static HFONT make_font(int height, int weight) {
    return CreateFontW(-height,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");
}
void ui_style_init(UiStyle *s, BOOL dark, BOOL large) {
    ZeroMemory(s,sizeof(*s)); s->dark=dark; s->large=large;
    s->bg=dark ? RGB(16,16,16) : RGB(248,248,248);
    s->fg=dark ? RGB(242,242,242) : RGB(28,28,28);
    s->muted=dark ? RGB(165,165,165) : RGB(99,99,99);
    s->panel_color=dark ? RGB(30,30,30) : RGB(239,239,239);
    s->input_color=dark ? RGB(22,22,22) : RGB(255,255,255);
    s->border=dark ? RGB(59,59,59) : RGB(204,204,204);
    s->accent=RGB(235,38,47);
    s->background=CreateSolidBrush(s->bg);
    s->panel=CreateSolidBrush(s->panel_color);
    s->input=CreateSolidBrush(s->input_color);
    s->normal=make_font(large ? 18 : 15,FW_NORMAL);
    s->bold=make_font(large ? 18 : 15,FW_BOLD);
    s->title=make_font(large ? 27 : 24,FW_BOLD);
    s->small=make_font(large ? 15 : 13,FW_NORMAL);
}
void ui_style_free(UiStyle *s) {
    if(s->normal) DeleteObject(s->normal);
    if(s->bold) DeleteObject(s->bold);
    if(s->title) DeleteObject(s->title);
    if(s->small) DeleteObject(s->small);
    if(s->background) DeleteObject(s->background);
    if(s->panel) DeleteObject(s->panel);
    if(s->input) DeleteObject(s->input);
    ZeroMemory(s,sizeof(*s));
}
void ui_set_fonts(const UiStyle *s, const UiControls *c) {
    HWND controls[]={c->query,c->search,c->paste,c->list,c->quality,c->mode,c->bridge,
        c->play,c->pause,c->cancel,c->retry,c->offline,c->settings,c->home,c->results_nav};
    unsigned i;
    for(i=0;i<sizeof(controls)/sizeof(controls[0]);++i)
        if(controls[i]) SendMessageW(controls[i],WM_SETFONT,(WPARAM)s->normal,TRUE);
    if(c->status) SendMessageW(c->status,WM_SETFONT,(WPARAM)s->small,TRUE);
    if(c->labels) SendMessageW(c->labels,WM_SETFONT,(WPARAM)s->small,TRUE);
    if(c->list) SendMessageW(c->list,LB_SETITEMHEIGHT,0,s->large ? 68 : 56);
    if(c->query) SendMessageW(c->query,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(10,10));
    if(c->bridge) SendMessageW(c->bridge,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
}
static void place(HWND h, int x, int y, int w, int height, BOOL visible) {
    if(!h) return;
    if(w>0 && height>0) MoveWindow(h,x,y,w,height,TRUE);
    ShowWindow(h,visible ? SW_SHOWNA : SW_HIDE);
}
/* The edit remains a single native control on both pages, preserving selection,
 * undo history and keyboard behavior when the page changes. */
void ui_layout(HWND window, const UiControls *c, const UiStyle *s,
               int w, int h, BOOL bridge_mode, BOOL video_active) {
    int m=w<720 ? 16 : 24, nav=h-55, foot=h-112;
    int compact=h<520, button=s->large ? 39 : 36, edit=s->large ? 23 : 20;
    int x, y, span, field, searchw, options, listw, content_bottom, listtop;
    BOOL home=s->page==UI_HOME;
    (void)window;
    place(c->settings,w-m-134,18,134,34,TRUE);
    place(c->home,w/2-(s->large ? 154 : 142),nav+5,s->large ? 148 : 136,43,TRUE);
    place(c->results_nav,w/2+6,nav+5,s->large ? 148 : 136,43,TRUE);
    place(c->labels,m,foot,w-2*m,18,TRUE);
    place(c->status,m,foot+22,w-2*m,32,TRUE);
    if(home) {
        span=smaller(w-2*m,690); x=(w-span)/2;
        y=compact ? (bridge_mode ? 155 : 186) : 235;
        if(h>590) y+=(h-590)/4;
        searchw=s->large ? 110 : 100; field=span-searchw-10;
        place(c->query,x+1,y+(44-edit)/2,field-2,edit,TRUE);
        place(c->search,x+field+10,y,searchw,44,TRUE);
        y+=compact ? 55 : 58;
        place(c->paste,x,y,(span-12)/2,button,TRUE);
        place(c->offline,x+(span+12)/2,y,(span-12)/2,button,TRUE);
        options=y+button+(compact ? 23 : 35);
        place(c->quality,x,options,(span-12)/2,180,TRUE);
        place(c->mode,x+(span+12)/2,options,(span-12)/2,180,TRUE);
        place(c->bridge,x,options+35,span,26,bridge_mode);
        place(c->list,0,0,0,0,FALSE); place(c->video,0,0,0,0,FALSE);
        place(c->play,0,0,0,0,FALSE); place(c->pause,0,0,0,0,FALSE);
        place(c->cancel,0,0,0,0,FALSE); place(c->retry,0,0,0,0,FALSE);
    } else {
        searchw=s->large ? 103 : 92;
        field=w-2*m-searchw-10;
        place(c->query,m+1,83+(40-edit)/2,field-2,edit,TRUE);
        place(c->search,m+field+10,83,searchw,40,TRUE);
        content_bottom=foot-105-(bridge_mode ? 31 : 0);
        listw=(w-2*m-16)*45/100;
        listtop=compact && bridge_mode ? 133 : 143;
        place(c->list,m,listtop,listw,larger(1,content_bottom-listtop),
              SendMessageW(c->list,LB_GETCOUNT,0,0)>0);
        place(c->video,m+listw+16,listtop,w-2*m-listw-16,
              larger(1,content_bottom-listtop),video_active);
        y=content_bottom+13;
        span=w-2*m; field=(span-3*8)/4;
        place(c->play,m,y,field,button,TRUE);
        place(c->pause,m+field+8,y,field,button,TRUE);
        place(c->cancel,m+2*(field+8),y,field,button,TRUE);
        place(c->retry,m+3*(field+8),y,span-3*(field+8),button,TRUE);
        y+=button+22;
        field=(span-16)*34/100;
        place(c->quality,m,y,field,180,TRUE);
        place(c->mode,m+field+8,y,field,180,TRUE);
        place(c->offline,m+2*(field+8),y,span-2*(field+8),29,TRUE);
        place(c->bridge,m,y+34,span,25,bridge_mode);
        place(c->paste,0,0,0,0,FALSE);
    }
}
static RECT child_rect(HWND window, HWND child) {
    RECT r={0,0,0,0};
    if(child) { GetWindowRect(child,&r); MapWindowPoints(NULL,window,(POINT *)&r,2); }
    return r;
}
static void input_outline(HWND window, HDC dc, HWND edit, const UiStyle *s, int top, int height) {
    RECT r=child_rect(window,edit); BOOL focused=GetFocus()==edit;
    r.top=top; r.bottom=top+height;
    round_box(dc,&r,12,s->input_color,focused ? s->accent : s->border);
}
void ui_paint(HWND window, HDC dc, const UiControls *c, const UiStyle *s,
              BOOL has_results, BOOL video_active) {
    RECT r, t, q, listrect; int w,h,m,save,y,span,x,options;
    BOOL compact,home=s->page==UI_HOME;
    BOOL bridge_mode=IsWindowVisible(c->bridge);
    GetClientRect(window,&r); w=r.right; h=r.bottom; m=w<720 ? 16 : 24;
    compact=h<520; save=SaveDC(dc);
    if(video_active && !home) {
        t=child_rect(window,c->video); ExcludeClipRect(dc,t.left,t.top,t.right,t.bottom);
    }
    FillRect(dc,&r,s->background);
    logo(dc,m,26,31,22,s->accent);
    t.left=m+42; t.top=20; t.right=w-m-146; t.bottom=54;
    label(dc,L"TubeLiviano",t,s->title,s->fg,DT_SINGLELINE|DT_VCENTER);
    line(dc,m,69,w-m,69,s->border);
    line(dc,m,h-55,w-m,h-55,s->border);
    if(home) {
        y=compact ? (bridge_mode ? 155 : 186) : 235;
        if(h>590) y+=(h-590)/4;
        if(compact && bridge_mode) logo(dc,w/2-23,80,46,31,s->accent);
        else logo(dc,w/2-34,compact ? 88 : y-125,68,46,s->accent);
        t.left=m; t.right=w-m; t.top=compact ? (bridge_mode ? 116 : 143) : y-65;
        t.bottom=t.top+34;
        label(dc,L"Buscá algo para empezar",t,s->title,s->fg,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(!compact) {
            t.top=y-29; t.bottom=y-8;
            label(dc,L"Videos y música, con lo que tu PC necesita.",t,s->normal,s->muted,DT_CENTER|DT_SINGLELINE);
        }
        input_outline(window,dc,c->query,s,y,44);
        span=smaller(w-2*m,690); x=(w-span)/2;
        options=y+(compact ? 55 : 58)+(s->large ? 39 : 36)+(compact ? 23 : 35);
        t.left=x; t.right=x+(span-12)/2; t.top=options-21; t.bottom=options-3;
        label(dc,L"Calidad",t,s->small,s->muted,DT_SINGLELINE);
        t.left=x+(span+12)/2; t.right=x+span;
        label(dc,L"Reproducción",t,s->small,s->muted,DT_SINGLELINE);
    } else {
        input_outline(window,dc,c->query,s,83,40);
        listrect=child_rect(window,c->list);
        q=child_rect(window,c->video);
        listrect.bottom=q.bottom;
        t=listrect; --t.left; --t.top; ++t.right; ++t.bottom;
        round_box(dc,&t,8,s->input_color,s->border);
        /* The player rectangle tracks the list; its black surface is only a
         * visible native child while VLC has a stream to display. */
        if(!video_active) {
            round_box(dc,&q,10,s->dark ? RGB(9,9,9) : RGB(231,231,231),s->border);
            logo(dc,(q.left+q.right)/2-23,(q.top+q.bottom)/2-30,46,32,s->accent);
            t=q; t.top=(q.top+q.bottom)/2+14; t.bottom=t.top+25;
            label(dc,L"Tu video se verá acá",t,s->small,s->muted,DT_CENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        }
        if(!has_results) {
            HGDIOBJ font; RECT measure; int height;
            t=listrect; t.left+=12; t.right-=12;
            measure=t; font=SelectObject(dc,s->normal);
            height=DrawTextW(dc,L"Buscá un video o pegá un enlace.",-1,&measure,
                             DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
            SelectObject(dc,font);
            t.top+=(t.bottom-t.top-height)/2; t.bottom=t.top+height;
            label(dc,L"Buscá un video o pegá un enlace.",t,s->normal,s->muted,DT_CENTER|DT_WORDBREAK);
        }
        t.left=m; t.right=m+130; t.top=listrect.bottom+13+(s->large ? 39 : 36)+3;
        t.bottom=t.top+18;
        label(dc,L"Calidad",t,s->small,s->muted,DT_SINGLELINE);
        t.left=m+((w-2*m-16)*34/100)+8; t.right=t.left+180;
        label(dc,L"Reproducción",t,s->small,s->muted,DT_SINGLELINE);
    }
    RestoreDC(dc,save);
}
HBRUSH ui_control_color(const UiStyle *s, UINT message, HDC dc, HWND control) {
    (void)control;
    SetBkMode(dc,OPAQUE);
    SetTextColor(dc,message==WM_CTLCOLORSTATIC ? s->muted : s->fg);
    if(message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX) {
        SetBkColor(dc,s->input_color); return s->input;
    }
    SetBkColor(dc,s->bg); return s->background;
}
BOOL ui_draw_button(const UiStyle *s, const DRAWITEMSTRUCT *item) {
    RECT r=item->rcItem,t=r,focus; WCHAR caption[160];
    BOOL disabled=(item->itemState&ODS_DISABLED)!=0, down=(item->itemState&ODS_SELECTED)!=0;
    BOOL nav=item->CtlID==HOME || item->CtlID==RESULTS_NAV;
    BOOL active=(item->CtlID==HOME && s->page==UI_HOME) ||
        (item->CtlID==RESULTS_NAV && s->page==UI_RESULTS);
    BOOL accent=item->CtlID==SEARCH || item->CtlID==PLAY;
    COLORREF bg=s->panel_color, fg=s->fg, edge=s->border;
    int saved,x,y;
    if(item->CtlType!=ODT_BUTTON) return FALSE;
    saved=SaveDC(item->hDC);
    GetWindowTextW(item->hwndItem,caption,sizeof(caption)/sizeof(caption[0]));
    if(accent) { bg=s->accent; fg=RGB(255,255,255); edge=bg; }
    if(disabled) { bg=s->panel_color; fg=s->muted; edge=s->border; }
    else if(down) { bg=accent ? RGB(187,22,30) : (s->dark ? RGB(57,57,57) : RGB(218,218,218)); edge=bg; }
    if(nav) {
        fill(item->hDC,&r,down ? s->panel_color : s->bg);
        fg=active ? s->accent : s->muted;
        if(active) line(item->hDC,r.left+30,r.top,r.right-30,r.top,s->accent);
        x=r.left+18; y=(r.top+r.bottom)/2;
        if(item->CtlID==HOME) {
            line(item->hDC,x,y-1,x+8,y-8,fg); line(item->hDC,x+8,y-8,x+16,y-1,fg);
            line(item->hDC,x+2,y-1,x+2,y+8,fg); line(item->hDC,x+2,y+8,x+14,y+8,fg);
            line(item->hDC,x+14,y+8,x+14,y-1,fg);
        } else {
            line(item->hDC,x,y-6,x+16,y-6,fg); line(item->hDC,x,y,x+16,y,fg);
            line(item->hDC,x,y+6,x+16,y+6,fg);
        }
        t.left+=40;
    } else round_box(item->hDC,&r,12,bg,edge);
    if(down) OffsetRect(&t,1,1);
    label(item->hDC,caption,t,s->normal,fg,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if((item->itemState&ODS_FOCUS) && !(item->itemState&ODS_NOFOCUSRECT)) {
        focus=r; InflateRect(&focus,-4,-4); SetTextColor(item->hDC,fg); DrawFocusRect(item->hDC,&focus);
    }
    RestoreDC(item->hDC,saved); return TRUE;
}
void ui_draw_result(const UiStyle *s, const DRAWITEMSTRUCT *item,
                    const EngineResult *results, int count) {
    RECT r=item->rcItem,t=r; WCHAR details[320],time[32];
    const EngineResult *result; int saved=SaveDC(item->hDC),duration;
    BOOL selected=(item->itemState&ODS_SELECTED)!=0;
    COLORREF bg=selected ? (s->dark ? RGB(55,33,35) : RGB(255,228,229)) : s->input_color;
    fill(item->hDC,&r,bg);
    if(item->itemID==(UINT)-1 || item->itemID>=(UINT)count) { RestoreDC(item->hDC,saved); return; }
    result=&results[item->itemID];
    if(selected) { RECT mark=r; mark.right=mark.left+3; fill(item->hDC,&mark,s->accent); }
    t.left+=13; t.right-=12; t.top+=s->large ? 10 : 7; t.bottom=t.top+(s->large ? 23 : 21);
    label(item->hDC,result->title,t,s->bold,s->fg,DT_SINGLELINE|DT_END_ELLIPSIS);
    duration=result->duration;
    if(duration>=3600) _snwprintf(time,32,L"%d:%02d:%02d",duration/3600,(duration/60)%60,duration%60);
    else if(duration>=0) _snwprintf(time,32,L"%d:%02d",duration/60,duration%60);
    else lstrcpyW(time,L"En vivo");
    time[31]=0;
    _snwprintf(details,320,L"%ls  ·  %ls",result->channel[0] ? result->channel : L"YouTube",time);
    details[319]=0; t.top=t.bottom+2; t.bottom=r.bottom-6;
    label(item->hDC,details,t,s->small,s->muted,DT_SINGLELINE|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS) { t=r; InflateRect(&t,-4,-4); DrawFocusRect(item->hDC,&t); }
    line(item->hDC,r.left+12,r.bottom-1,r.right-12,r.bottom-1,s->panel_color);
    RestoreDC(item->hDC,saved);
}
