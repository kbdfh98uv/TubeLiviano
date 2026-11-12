#ifndef TUBELIVIANO_UI_H
#define TUBELIVIANO_UI_H
#include <windows.h>
#include "engine.h"

enum { QUERY=100, SEARCH, PASTE, RESULTS, QUALITY, MODE, BRIDGE, PLAY,
       PAUSE, CANCEL, RETRY, OFFLINE, SETTINGS, HOME, RESULTS_NAV, FULLSCREEN };
typedef enum { UI_HOME=0, UI_RESULTS=1 } UiPage;
typedef struct {
    HWND query, search, paste, list, quality, mode, bridge;
    HWND play, pause, cancel, retry, offline, status, labels, video;
    HWND settings, home, results_nav, fullscreen;
} UiControls;
typedef struct {
    BOOL dark, large, fullscreen;
    UiPage page;
    HFONT normal, bold, title, small;
    HBRUSH background, panel, input;
    COLORREF bg, fg, muted, panel_color, input_color, border, accent;
} UiStyle;

void ui_style_init(UiStyle *style, BOOL dark, BOOL large);
void ui_style_free(UiStyle *style);
void ui_set_fonts(const UiStyle *style, const UiControls *controls);
void ui_layout(HWND window, const UiControls *controls, const UiStyle *style,
               int width, int height, BOOL bridge_mode, BOOL video_active);
void ui_paint(HWND window, HDC dc, const UiControls *controls, const UiStyle *style,
              BOOL has_results, BOOL video_active);
HBRUSH ui_control_color(const UiStyle *style, UINT message, HDC dc, HWND control);
BOOL ui_draw_button(const UiStyle *style, const DRAWITEMSTRUCT *item);
void ui_draw_result(const UiStyle *style, const DRAWITEMSTRUCT *item,
                    const EngineResult *results, int count);
#endif
