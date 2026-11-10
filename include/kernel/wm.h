/* wm.h - Window Manager */
#pragma once
#include <kernel/types.h>
#include <kernel/graphics.h>
#include <kernel/ui_theme.h>

#define MAX_WINDOWS 16
#define WM_MAX_TITLE_LENGTH 127
#define WINDOW_TITLEBAR_HEIGHT 28

#define COLOR_SURFACE CHARIS_UI_PANEL
#define COLOR_BORDER  CHARIS_UI_BORDER
#define COLOR_ACCENT  CHARIS_UI_ACCENT

typedef struct window {
    int x, y;
    int width, height;
    u32 z_order;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    char title[128];
    u32* buffer;
    struct window* next;
    void (*on_paint)(struct window*);
    void (*on_event)(struct window*, int event_type, void* data);
} window_t;

extern window_t* wm_windows;

void wm_init(void);
window_t* wm_create_window(const char* title, int x, int y, int w, int h);
window_t* wm_create_surface_window(const char* title, int x, int y, int w, int h);
void wm_destroy_window(window_t* win);
void wm_focus_window(window_t* win);

/* Geometry and visibility operations validate ownership and screen bounds. */
bool wm_move_window(window_t* win, int x, int y);
bool wm_resize_window(window_t* win, int width, int height);
bool wm_minimize_window(window_t* win);
bool wm_restore_window(window_t* win);
bool wm_set_window_visible(window_t* win, bool visible);
window_t* wm_focus_next(void);
u32 wm_get_window_count(void);

void wm_render(void);
void wm_dispatch_event(int event_type, void* data);
window_t* wm_get_focused(void);
window_t* wm_get_window_at(int x, int y);
void wm_draw_decorations(window_t* win);
void wm_draw_contents(window_t* win);
void wm_process_mouse(int x, int y, int button, bool pressed);
void wm_main_loop(void);
void wm_set_screen_bounds(uint32_t width, uint32_t height);
