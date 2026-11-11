/* shell_ui.c - Graphical shell: wallpaper, taskbar, start menu
 *
 * Runs as its own kernel task. Each pass drains the input queue, updates the UI
 * state, and redraws only what changed. There is no compositor underneath, so
 * the whole screen is redrawn on a change; at 1024x768 that is cheap enough,
 * and a partial-repaint layer can replace it if it ever is not.
 *
 * Input arrives from two places. The mouse is an IRQ handler that updates
 * g_mouse, and keyboard scancodes are pushed by the hook installed in
 * ui_install_input_hooks(). Both are turned into input events and consumed here.
 */

#include <kernel/shell_ui.h>
#include <kernel/browser.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/input.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/timer.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>
#include <kernel/scheduler.h>
#include <kernel/printf.h>
#include <kernel/wallpaper.h>
#include <kernel/config.h>
#include <kernel/wizard.h>
#include <kernel/terminal_ui.h>
#include <kernel/files_ui.h>
#include <kernel/wm.h>
#include <kernel/serial.h>

/* Native desktop widgets and launcher hit-testing. */
extern void desktop_surface_render(void);
extern int desktop_surface_hit_test(int mx, int my);
extern void desktop_surface_draw_taskbar_icons(u32 y);
extern int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y);

static u32 ui_px(u32 logical) { return gfx_ui_scale(logical); }
static u32 text_height(void) { return font_cell_h * gfx_text_get_scale(); }
static u32 taskbar_height(void) {
    u32 base = ui_px(48), needed = text_height() + ui_px(12);
    return base > needed ? base : needed;
}
static u32 start_button_width(void) {
    u32 width = 0;
    gfx_text_size("Charis", &width, NULL);
    u32 needed = width + ui_px(28), base = ui_px(64);
    return base > needed ? base : needed;
}
static u32 menu_width(void) {
    u32 width = 0;
    gfx_text_size("About CharisOS", &width, NULL);
    u32 needed = width + ui_px(64), base = ui_px(260);
    return base > needed ? base : needed;
}
static u32 menu_item_height(void) {
    u32 base = ui_px(34), needed = text_height() + ui_px(12);
    return base > needed ? base : needed;
}
#define TASKBAR_H taskbar_height()
#define START_BTN_W start_button_width()
#define MENU_W menu_width()
#define MENU_ITEM_H menu_item_height()

/* ── Shared solid-morphism theme ─────────────────────────────────── */
#define T_BG          CHARIS_UI_BACKGROUND
#define T_TASKBAR     CHARIS_UI_PANEL
#define T_START_BTN   CHARIS_UI_PANEL_RAISED
#define T_START_HOVER CHARIS_UI_SURFACE_RAISED
#define T_MENU        CHARIS_UI_PANEL
#define T_MENU_ITEM   CHARIS_UI_SURFACE
#define T_MENU_HOVER  CHARIS_UI_ACCENT_SURFACE
#define T_FG          CHARIS_UI_TEXT
#define T_FG_DIM      CHARIS_UI_MUTED
#define T_BORDER      CHARIS_UI_BORDER
#define T_CURSOR      CHARIS_UI_TEXT

/* Layout. */
#define CURSOR_BLINK  400   /* ms */

typedef struct {
    const char* label;
} menu_entry_t;

static const menu_entry_t MENU_ITEMS[] = {
    { "Terminal" },
    { "Files" },
    { "Settings" },
    { "Browser" },
    { "About CharisOS" },
};

static bool notice_open = false;
static int notice_item = -1;
static bool settings_open = false;

static const char* const NOTICE_LINE_1[] = {
    "", "", "", "",
    "CharisOS is an independent x86_64 operating system.",
};
static const char* const NOTICE_LINE_2[] = {
    "", "", "", "",
    "Built from scratch in C and NASM.",
};
#define MENU_ITEM_COUNT ((u32)(sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0])))

typedef struct {
    bool     menu_open;
    int      hover_item;      /* -1 for none */
    bool     cursor_blink;
    u64      next_blink_ms;
    u64      frame;
    bool     needs_redraw;
} ui_state_t;

static ui_state_t ui = { false, -1, false, 0, 0, true };

/* The shell owns application lifecycle; the WM owns focus, geometry,
 * clipping, z-order and repaint order. Native app renderers paint directly
 * into their assigned clipped framebuffer rectangle. */
typedef enum {
    SHELL_APP_TERMINAL = 0,
    SHELL_APP_FILES,
    SHELL_APP_BROWSER,
    SHELL_APP_SETTINGS,
    SHELL_APP_ABOUT,
    SHELL_APP_COUNT
} shell_app_id_t;

static window_t* shell_app_windows[SHELL_APP_COUNT];
static window_t* shell_drag_window;
static int shell_drag_offset_x, shell_drag_offset_y;
static window_t* shell_resize_window;
static int shell_resize_origin_x, shell_resize_origin_y;
static int shell_resize_start_width, shell_resize_start_height;
static bool shell_pointer_handled;
static bool alt_held;

static u32 settings_title_height(void);
static u32 settings_panel_width(void);
static u32 settings_panel_height(void);
static u32 about_titlebar_height(void);
static void draw_settings_window(window_t* win);
static bool settings_handle_window_click(window_t* win, int mx, int my);
static void draw_about_window(window_t* win);
static bool about_handle_window_click(window_t* win, int mx, int my);

static int shell_app_id_for_window(const window_t* win) {
    for (u32 i = 0; i < SHELL_APP_COUNT; i++)
        if (shell_app_windows[i] == win) return (int)i;
    return -1;
}

static bool shell_app_is_open(shell_app_id_t id) {
    switch (id) {
        case SHELL_APP_TERMINAL: return terminal_is_open();
        case SHELL_APP_FILES: return files_is_open();
        case SHELL_APP_BROWSER: return browser_is_open();
        case SHELL_APP_SETTINGS: return settings_open;
        case SHELL_APP_ABOUT: return notice_open && notice_item == 4;
        default: return false;
    }
}

static const char* shell_app_title(shell_app_id_t id) {
    switch (id) {
        case SHELL_APP_TERMINAL: return "Terminal";
        case SHELL_APP_FILES: return "Files";
        case SHELL_APP_BROWSER: return "Browser";
        case SHELL_APP_SETTINGS: return "Settings";
        case SHELL_APP_ABOUT: return "About CharisOS";
        default: return "Application";
    }
}

static u32 shell_app_titlebar_height(shell_app_id_t id) {
    switch (id) {
        case SHELL_APP_TERMINAL: return terminal_titlebar_height();
        case SHELL_APP_FILES: return files_titlebar_height();
        case SHELL_APP_BROWSER: return browser_titlebar_height();
        case SHELL_APP_SETTINGS: return settings_title_height();
        case SHELL_APP_ABOUT: return about_titlebar_height();
        default: return WINDOW_TITLEBAR_HEIGHT;
    }
}

static void shell_draw_resize_grip(const window_t* win) {
    if (!win || win->maximized || win->width < (int)ui_px(18) ||
        win->height < (int)ui_px(18)) return;
    u32 right = (u32)win->x + (u32)win->width - ui_px(4);
    u32 bottom = (u32)win->y + win->titlebar_height +
                 (u32)win->height - ui_px(4);
    for (u32 i = 0; i < 3; i++) {
        u32 inset = ui_px(5u + i * 4u);
        u32 length = ui_px(3u + i * 2u);
        fb_fill_rect(right - inset, bottom, length, ui_px(1),
                     CHARIS_UI_BORDER_STRONG);
        fb_fill_rect(right, bottom - inset, ui_px(1), length,
                     CHARIS_UI_BORDER_STRONG);
    }
}

static bool shell_resize_grip_hit(const window_t* win, int mx, int my) {
    if (!win || win->maximized) return false;
    u32 grip = ui_px(18);
    int right = win->x + win->width;
    int bottom = win->y + (int)win->titlebar_height + win->height;
    return mx >= right - (int)grip && mx < right &&
           my >= bottom - (int)grip && my < bottom;
}

static void shell_app_paint(window_t* win) {
    int id = shell_app_id_for_window(win);
    if (id < 0 || !win || !win->visible || win->minimized) return;
    u32 full_height = (u32)win->height + win->titlebar_height;
    switch ((shell_app_id_t)id) {
        case SHELL_APP_TERMINAL:
            terminal_set_bounds((u32)win->x, (u32)win->y,
                                (u32)win->width, full_height);
            terminal_render();
            break;
        case SHELL_APP_FILES:
            files_set_bounds((u32)win->x, (u32)win->y,
                             (u32)win->width, full_height);
            files_render();
            break;
        case SHELL_APP_BROWSER:
            browser_set_bounds((u32)win->x, (u32)win->y,
                               (u32)win->width, full_height);
            browser_render();
            break;
        case SHELL_APP_SETTINGS:
            draw_settings_window(win);
            break;
        case SHELL_APP_ABOUT:
            draw_about_window(win);
            break;
        default: break;
    }
    shell_draw_resize_grip(win);
}

static void shell_app_event(window_t* win, int event_type, void* data) {
    int id = shell_app_id_for_window(win);
    if (id < 0 || !data) return;

    if (event_type == INPUT_MOUSE_DOWN) {
        const input_event_t* evt = (const input_event_t*)data;
        switch ((shell_app_id_t)id) {
            case SHELL_APP_TERMINAL:
                shell_pointer_handled = terminal_handle_mouse(evt->mouse.x, evt->mouse.y);
                break;
            case SHELL_APP_FILES:
                shell_pointer_handled = files_handle_mouse(evt->mouse.x, evt->mouse.y);
                break;
            case SHELL_APP_BROWSER:
                shell_pointer_handled = browser_handle_mouse(evt->mouse.x, evt->mouse.y);
                break;
            case SHELL_APP_SETTINGS:
                shell_pointer_handled = settings_handle_window_click(
                    win, evt->mouse.x, evt->mouse.y);
                break;
            case SHELL_APP_ABOUT:
                shell_pointer_handled = about_handle_window_click(
                    win, evt->mouse.x, evt->mouse.y);
                break;
            default: break;
        }
        return;
    }

    if (event_type != INPUT_KEY_DOWN && event_type != INPUT_KEY_UP) return;
    const input_event_t* evt = (const input_event_t*)data;
    bool pressed = event_type == INPUT_KEY_DOWN;
    u8 scancode = evt->key.scancode;
    u8 base = scancode & 0x7Fu;
    bool extended = (scancode & 0x80u) != 0;

    switch ((shell_app_id_t)id) {
        case SHELL_APP_TERMINAL:
            if (pressed && !extended && base == KEY_ESCAPE) terminal_close();
            break;
        case SHELL_APP_FILES:
            (void)files_handle_key((u8)(base | (extended ? 0x80u : 0u)), pressed);
            break;
        case SHELL_APP_BROWSER:
            (void)browser_handle_key(base, pressed);
            break;
        case SHELL_APP_SETTINGS:
            if (pressed && !extended && base == KEY_ESCAPE) {
                settings_open = false;
            } else if (pressed && !extended && base == 0x40u) {
                int value = config_get_int("font_scale", 1) + 1;
                if (value > 3) value = 3;
                config_set_int("font_scale", value);
                gfx_text_apply_preferences();
                config_save(CONFIG_FILE_PATH);
            } else if (pressed && !extended && base == 0x41u) {
                int value = config_get_int("ui_scale", 100) + 20;
                if (value > 120) value = 120;
                config_set_int("ui_scale", value);
                gfx_text_apply_preferences();
                config_save(CONFIG_FILE_PATH);
            }
            break;
        case SHELL_APP_ABOUT:
            if (pressed && !extended && base == KEY_ESCAPE) {
                notice_open = false;
                notice_item = -1;
            }
            break;
        default: break;
    }
}

static window_t* shell_create_app_window(shell_app_id_t id) {
    if (!g_framebuffer.initialized) return NULL;
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    if (sw < 320u || sh < 220u) return NULL;

    u32 title_h = shell_app_titlebar_height(id);
    /* Give applications useful, different default footprints. A newly opened
     * window should not bury every earlier window under a full-screen sheet. */
    u32 width = ui_px(680);
    u32 height = ui_px(460);
    switch (id) {
        case SHELL_APP_FILES:
            width = ui_px(720);
            height = ui_px(520);
            break;
        case SHELL_APP_TERMINAL:
            width = ui_px(640);
            height = ui_px(440);
            break;
        case SHELL_APP_BROWSER:
            width = ui_px(840);
            height = ui_px(560);
            break;
        case SHELL_APP_SETTINGS:
            width = settings_panel_width();
            height = settings_panel_height();
            break;
        case SHELL_APP_ABOUT:
            width = ui_px(560);
            height = ui_px(280);
            break;
        default:
            break;
    }

    u32 max_width = sw > ui_px(24) ? sw - ui_px(24) : sw;
    u32 max_height = sh > ui_px(24) ? sh - ui_px(24) : sh;
    if (width > max_width) width = max_width;
    if (height > max_height) height = max_height;
    if (height <= title_h + 64u || width <= ui_px(160)) return NULL;

    s32 x = (s32)((sw - width) / 2u);
    s32 y = (s32)((sh - height) / 2u);
    switch (id) {
        case SHELL_APP_FILES:
            x -= (s32)ui_px(36);
            y -= (s32)ui_px(24);
            break;
        case SHELL_APP_TERMINAL:
            x += (s32)ui_px(36);
            y += (s32)ui_px(24);
            break;
        case SHELL_APP_BROWSER:
            x += (s32)ui_px(54);
            y -= (s32)ui_px(12);
            break;
        case SHELL_APP_SETTINGS:
            x -= (s32)ui_px(24);
            y += (s32)ui_px(12);
            break;
        case SHELL_APP_ABOUT:
        default:
            break;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((u32)x + width > sw) x = (s32)(sw - width);
    if ((u32)y + height > sh) y = (s32)(sh - height);

    window_t* win = wm_create_surface_window(
        shell_app_title(id), (int)x, (int)y, (int)width,
        (int)(height - title_h));
    if (!win) return NULL;
    if (!wm_set_titlebar_height(win, title_h)) {
        wm_destroy_window(win);
        return NULL;
    }
    win->on_paint = shell_app_paint;
    win->on_event = shell_app_event;
    return win;
}

static void shell_sync_app_windows(void) {
    for (u32 i = 0; i < SHELL_APP_COUNT; i++) {
        shell_app_id_t id = (shell_app_id_t)i;
        if (!shell_app_is_open(id)) {
            if (shell_app_windows[i]) {
                if (shell_drag_window == shell_app_windows[i]) shell_drag_window = NULL;
                if (shell_resize_window == shell_app_windows[i]) shell_resize_window = NULL;
                wm_destroy_window(shell_app_windows[i]);
                shell_app_windows[i] = NULL;
            }
            continue;
        }
        if (!shell_app_windows[i])
            shell_app_windows[i] = shell_create_app_window(id);
    }
}

static void shell_open_app(shell_app_id_t id) {
    if (shell_app_windows[id]) {
        window_t* win = shell_app_windows[id];
        if (win->minimized) (void)wm_restore_window(win);
        else wm_focus_window(win);
        return;
    }
    switch (id) {
        case SHELL_APP_TERMINAL: terminal_open(); break;
        case SHELL_APP_FILES: files_open(); break;
        case SHELL_APP_BROWSER: browser_open(); break;
        case SHELL_APP_SETTINGS: settings_open = true; break;
        case SHELL_APP_ABOUT:
            notice_item = 4;
            notice_open = true;
            break;
        default: break;
    }
    shell_sync_app_windows();
    if (shell_app_windows[id]) wm_focus_window(shell_app_windows[id]);
}

static void shell_toggle_app(shell_app_id_t id) {
    window_t* win = shell_app_windows[id];
    if (!win) {
        shell_open_app(id);
        return;
    }
    if (win->minimized) (void)wm_restore_window(win);
    else if (wm_get_focused() == win) (void)wm_minimize_window(win);
    else wm_focus_window(win);
}

/* ── Chrome ───────────────────────────────────────────────────────── */

static void draw_taskbar(void) {
    u32 w = g_framebuffer.width;
    u32 h = g_framebuffer.height;
    u32 bar_h = taskbar_height();
    u32 y = h > bar_h ? h - bar_h : 0;

    fb_fill_rect(0, y, w, bar_h, T_TASKBAR);
    fb_fill_rect(0, y, w, 1, T_BORDER);

    /* Start button. */
    bool hovered = input_get_mouse_y() >= (int)y && input_get_mouse_x() < (int)START_BTN_W;
    u32 bg = ui.menu_open ? T_MENU_HOVER
            : hovered     ? T_START_HOVER : T_START_BTN;
    fb_fill_rect(0, y, START_BTN_W, bar_h, bg);
    fb_draw_rect(0, y, START_BTN_W, bar_h, T_BORDER);

    /* Flat CharisOS mark. The wordmark belongs on boot and About screens,
     * not inside the running desktop. */
    u32 mark_size = ui_px(24);
    u32 mark_x = (START_BTN_W > mark_size ? (START_BTN_W - mark_size) / 2 : 0);
    u32 mark_y = y + (bar_h > mark_size ? (bar_h - mark_size) / 2 : 0);
    fb_fill_rect(mark_x, mark_y, mark_size, mark_size, CHARIS_UI_ACCENT);
    fb_fill_rect(mark_x + ui_px(5), mark_y + ui_px(5), ui_px(14), ui_px(14), T_START_BTN);
    fb_fill_rect(mark_x + ui_px(5), mark_y + ui_px(5), ui_px(4), ui_px(14), CHARIS_UI_ACCENT);
    fb_fill_rect(mark_x + ui_px(5), mark_y + ui_px(5), ui_px(14), ui_px(4), CHARIS_UI_ACCENT);
    fb_fill_rect(mark_x + ui_px(5), mark_y + ui_px(15), ui_px(14), ui_px(4), CHARIS_UI_ACCENT);
    desktop_surface_draw_taskbar_icons(y);
    static const shell_app_id_t taskbar_apps[4] = {
        SHELL_APP_FILES, SHELL_APP_TERMINAL,
        SHELL_APP_SETTINGS, SHELL_APP_BROWSER
    };
    static const u32 taskbar_x[4] = { 112u, 156u, 200u, 244u };
    for (u32 i = 0; i < 4; i++) {
        window_t* win = shell_app_windows[taskbar_apps[i]];
        if (!win) continue;
        u32 mark = win->minimized ? CHARIS_UI_MUTED :
                   win->focused ? CHARIS_UI_ACCENT : CHARIS_UI_BORDER_STRONG;
        fb_fill_rect(ui_px(taskbar_x[i] + 4u),
                     y + ui_px(44u) - ui_px(3u),
                     ui_px(20u), ui_px(2u), mark);
    }

    /* Do not label uptime as a clock. A real wall clock will be added with
     * the RTC/timezone service; the taskbar stays clear until then. */
}

/* Every launcher entry gets a small, high-contrast icon drawn from
 * primitives. This avoids a font/icon asset dependency in the kernel. */
static void draw_app_icon(u32 app, u32 x, u32 y) {
    u32 ink = app == 0 ? CHARIS_UI_ACCENT :
              app == 1 ? 0xFF91D7A3u :
              app == 2 ? 0xFFF0C674u :
              app == 3 ? 0xFFB7A0F5u :
              app == 4 ? 0xFF8FC7D8u : CHARIS_UI_TEXT;
    fb_fill_rect(x, y, 20, 20, CHARIS_UI_PANEL_RAISED);
    fb_draw_rect(x, y, 20, 20, CHARIS_UI_BORDER);
    if (app == 0) {
        fb_draw_rect(x + 3, y + 3, 14, 14, ink);
        fb_fill_rect(x + 3, y + 7, 14, 2, ink);
        fb_fill_rect(x + 9, y + 4, 2, 12, ink);
    } else if (app == 1) {
        gfx_draw_string(x + 2, y + 4, ">_", ink, FB_TRANSPARENT);
    } else if (app == 2) {
        fb_fill_rect(x + 3, y + 5, 7, 3, ink);
        fb_fill_rect(x + 3, y + 7, 14, 10, ink);
        fb_draw_rect(x + 3, y + 7, 14, 10, CHARIS_UI_BORDER);
    } else if (app == 3) {
        fb_fill_rect(x + 4, y + 2, 12, 16, ink);
        fb_fill_rect(x + 6, y + 6, 8, 1, CHARIS_UI_PANEL);
        fb_fill_rect(x + 6, y + 10, 8, 1, CHARIS_UI_PANEL);
        fb_fill_rect(x + 6, y + 14, 6, 1, CHARIS_UI_PANEL);
    } else if (app == 4) {
        fb_draw_rect(x + 4, y + 4, 12, 12, ink);
        fb_fill_rect(x + 8, y + 1, 4, 18, ink);
        fb_fill_rect(x + 1, y + 8, 18, 4, ink);
        fb_fill_rect(x + 7, y + 7, 6, 6, CHARIS_UI_PANEL_RAISED);
    } else {
        fb_draw_rect(x + 5, y + 2, 10, 16, ink);
        gfx_draw_string(x + 9, y + 4, "i", ink, FB_TRANSPARENT);
    }
}

static void draw_menu(void) {
    if (!ui.menu_open) return;

    u32 h = g_framebuffer.height;
    u32 menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + ui_px(16);
    u32 bar_h = taskbar_height();
    u32 y = h > bar_h + menu_h ? h - bar_h - menu_h : 0;
    u32 x = 0;

    fb_fill_rect(x + ui_px(4), y + ui_px(4), MENU_W, menu_h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, MENU_W, menu_h, T_MENU);
    fb_draw_rect(x, y, MENU_W, menu_h, T_BORDER);

    for (u32 i = 0; i < MENU_ITEM_COUNT; i++) {
        u32 iy = y + ui_px(8) + i * MENU_ITEM_H;
        bool hovered = (ui.hover_item == (int)i);
        fb_fill_rect(x + ui_px(2), iy, MENU_W - ui_px(4), MENU_ITEM_H,
                     hovered ? T_MENU_HOVER : T_MENU_ITEM);
        draw_app_icon(i + 1u, x + ui_px(12), iy + ui_px(7));
        gfx_draw_string(x + ui_px(44), iy + (MENU_ITEM_H > text_height() ? (MENU_ITEM_H - text_height()) / 2 : 0),
                        MENU_ITEMS[i].label, T_FG, FB_TRANSPARENT);
    }
}

/* A launcher item must not silently send output to an invisible VGA console.
 * Until each application has a real framebuffer UI, explain its current state
 * in a consistent, dismissible panel instead of pretending it launched. */
static u32 about_titlebar_height(void) { return ui_px(42); }

static void draw_about_window(window_t* win) {
    if (!win || !notice_open || notice_item != 4) return;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 w = (u32)win->width;
    u32 h = (u32)win->height + win->titlebar_height;
    u32 title_h = win->titlebar_height;

    fb_fill_rect(x + ui_px(5), y + ui_px(6), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(x, y, w, title_h, CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x, y + title_h, w, ui_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(x + ui_px(18), y + (title_h - text_height()) / 2u,
                    "About CharisOS", CHARIS_UI_TEXT, FB_TRANSPARENT);

    u32 close_x = x + w - ui_px(38);
    u32 close_y = y + (title_h - ui_px(26)) / 2u;
    fb_fill_rect(close_x, close_y, ui_px(26), ui_px(26), CHARIS_UI_SURFACE);
    fb_draw_rect(close_x, close_y, ui_px(26), ui_px(26), CHARIS_UI_BORDER);
    gfx_draw_string(close_x + ui_px(9), close_y + ui_px(5), "x",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);

    gfx_draw_string(x + ui_px(24), y + title_h + ui_px(34),
                    NOTICE_LINE_1[4], CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + ui_px(24), y + title_h + ui_px(64),
                    NOTICE_LINE_2[4], CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + ui_px(24), y + title_h + ui_px(108),
                    "Experimental software. Not a production operating system.",
                    CHARIS_UI_WARNING, FB_TRANSPARENT);
    fb_fill_rect(x + ui_px(20), y + h - ui_px(40),
                 w - ui_px(40), ui_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(x + ui_px(24), y + h - ui_px(27),
                    "x86_64   /   Multiboot2   /   Freestanding C + NASM",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
}

static bool about_handle_window_click(window_t* win, int mx, int my) {
    if (!win) return false;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 w = (u32)win->width, h = (u32)win->height + win->titlebar_height;
    u32 title_h = win->titlebar_height;
    if (mx < (int)x || mx >= (int)(x + w) ||
        my < (int)y || my >= (int)(y + h)) return false;
    if (my < (int)(y + title_h)) {
        if (mx >= (int)(x + w - ui_px(44))) {
            notice_open = false;
            notice_item = -1;
            return true;
        }
        return false; /* unclaimed titlebar space remains draggable */
    }
    return true;
}

/* Native Settings application: navigation rail plus a focused settings page. */
static u32 settings_title_height(void) { return ui_px(52); }
static u32 settings_row_height(void) {
    u32 minimum = ui_px(100);
    u32 readable = text_height() * 2u + ui_px(28);
    return minimum > readable ? minimum : readable;
}
static u32 settings_row_height_for(u32 full_height) {
    u32 desired = settings_row_height();
    u32 fixed = settings_title_height() + ui_px(128) + ui_px(46);
    if (full_height <= fixed) return ui_px(64);
    u32 available = (full_height - fixed) / 3u;
    return desired < available ? desired : available;
}
static u32 settings_button_width(void) { return ui_px(32); }
static u32 settings_panel_width(void) {
    u32 desired = ui_px(760);
    u32 available = g_framebuffer.width > ui_px(24)
                  ? g_framebuffer.width-ui_px(24) : g_framebuffer.width;
    return desired > available ? available : desired;
}
static u32 settings_panel_height(void) {
    return settings_title_height() + ui_px(90) +
           (3 * settings_row_height()) + ui_px(58);
}

static void draw_settings_window(window_t* win) {
    if (!win || !settings_open) return;
    static const char* const styles[] = { "Proportional", "Monospace", "Bitmap serif" };
    static const char* const scales[] = { "100%", "200%", "300%" };
    static const char* const ui_scales[] = { "80%", "100%", "120%" };
    u32 cw = (u32)win->width;
    u32 ch = (u32)win->height + win->titlebar_height;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 title_h = settings_title_height();
    u32 row_h = settings_row_height_for(ch);
    u32 rail_w = ui_px(164);
    if (rail_w > cw/3u) rail_w = cw/3u;
    u32 content_x = x+rail_w+ui_px(28);
    u32 button_w = settings_button_width();
    u32 plus_x = x+cw-button_w-ui_px(24);
    u32 control_x = plus_x-button_w-ui_px(8);
    u32 value_x = content_x + (control_x-content_x)/2u;
    u32 button_h = ui_px(34);

    fb_fill_rect(x+ui_px(7), y+ui_px(8), cw, ch, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, cw, ch, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, cw, ch, CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(x, y, cw, title_h, CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x, y+title_h, cw, ui_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(x+ui_px(22), y+(title_h-text_height())/2u,
                    "Settings", CHARIS_UI_TEXT, FB_TRANSPARENT);
    if (text_height() <= ui_px(16)) {
        fb_set_clip(x+ui_px(104), y,
                    cw>ui_px(168) ? cw-ui_px(168) : 1, title_h);
        gfx_draw_string(x+ui_px(104), y+(title_h-text_height())/2u,
                        "SYSTEM PREFERENCES", CHARIS_UI_MUTED, FB_TRANSPARENT);
        fb_reset_clip();
    }
    u32 close_x = x+cw-ui_px(42), close_y = y+ui_px(12);
    fb_fill_rect(close_x, close_y, ui_px(26), ui_px(26), CHARIS_UI_SURFACE);
    fb_draw_rect(close_x, close_y, ui_px(26), ui_px(26), CHARIS_UI_BORDER);
    gfx_draw_string(close_x+ui_px(8), close_y+ui_px(5), "x",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);

    fb_fill_rect(x+ui_px(1), y+title_h+ui_px(1), rail_w,
                 ch>title_h+ui_px(2) ? ch-title_h-ui_px(2) : 1u,
                 CHARIS_UI_PANEL_RAISED);
    gfx_draw_string(x+ui_px(18), y+title_h+ui_px(24),
                    "PREFERENCES", CHARIS_UI_MUTED, FB_TRANSPARENT);
    const char* nav[] = {
        "Display",
        text_height() > ui_px(16) ? "Personal" : "Personalization",
        "System"
    };
    for (u32 i=0; i<3; i++) {
        u32 ny = y+title_h+ui_px(54)+i*ui_px(42);
        if (i==0) {
            fb_fill_rect(x+ui_px(8), ny-ui_px(8), rail_w-ui_px(16),
                         ui_px(34), CHARIS_UI_ACCENT_SURFACE);
            fb_fill_rect(x+ui_px(8), ny-ui_px(8), ui_px(3),
                         ui_px(34), CHARIS_UI_ACCENT);
        }
        fb_set_clip(x+ui_px(18), ny-ui_px(4),
                    rail_w>ui_px(28) ? rail_w-ui_px(28) : 1, ui_px(42));
        gfx_draw_string(x+ui_px(22), ny, nav[i],
                        i==0 ? CHARIS_UI_TEXT : CHARIS_UI_MUTED,
                        FB_TRANSPARENT);
        fb_reset_clip();
    }

    u32 page_heading_y = y+title_h+ui_px(18);
    gfx_draw_string(content_x, page_heading_y,
                    "Display", CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(content_x, page_heading_y+text_height()+ui_px(6),
                    "Control the way CharisOS looks and reads.",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    int style = config_get_int("font_style", 0);
    int font_scale = config_get_int("font_scale", 1);
    int global_scale = config_get_int("ui_scale", 100);
    if (style<0 || style>2) style=0;
    if (font_scale<1 || font_scale>3) font_scale=1;
    int scale_index = global_scale<=80 ? 0 : global_scale>=120 ? 2 : 1;
    const char* values[3] = { styles[style], scales[font_scale-1], ui_scales[scale_index] };
    const char* labels[3] = { "Font family", "Text size", "Interface scale" };
    const char* hints[3] = {
        "Native app typeface",
        "Desktop text size",
        "Window and control size"
    };
    for (u32 i=0; i<3; i++) {
        u32 ry = y+title_h+ui_px(82)+i*row_h;
        u32 row_w = cw>rail_w+ui_px(30) ? cw-rail_w-ui_px(30) : 1;
        fb_fill_rect(content_x-ui_px(10), ry, row_w,
                     row_h>ui_px(10) ? row_h-ui_px(10) : 1u, CHARIS_UI_SURFACE);
        fb_draw_rect(content_x-ui_px(10), ry, row_w,
                     row_h>ui_px(10) ? row_h-ui_px(10) : 1u, CHARIS_UI_BORDER);
        u32 label_x = content_x+ui_px(8);
        u32 label_w = value_x > label_x+ui_px(12)
                    ? value_x-label_x-ui_px(12) : 1;
        u32 row_draw_h = row_h>ui_px(10) ? row_h-ui_px(10) : 1u;
        fb_set_clip(label_x, ry, label_w, row_draw_h);
        u32 label_y = font_scale > 1
                    ? ry+(row_h-text_height())/2u
                    : ry+ui_px(14);
        gfx_draw_string(label_x, label_y,
                        labels[i], CHARIS_UI_TEXT, FB_TRANSPARENT);
        if (font_scale == 1)
            gfx_draw_string(label_x, label_y+text_height()+ui_px(6),
                            hints[i], CHARIS_UI_MUTED, FB_TRANSPARENT);
        fb_reset_clip();
        u32 by = ry+(row_h-button_h)/2u;
        fb_fill_rect(control_x, by, button_w, button_h,
                     CHARIS_UI_PANEL_RAISED);
        fb_draw_rect(control_x, by, button_w, button_h, CHARIS_UI_BORDER);
        fb_fill_rect(plus_x, by, button_w, button_h,
                     CHARIS_UI_ACCENT_SURFACE);
        fb_draw_rect(plus_x, by, button_w, button_h,
                     CHARIS_UI_BORDER_STRONG);
        gfx_draw_string(control_x+ui_px(12), by+(button_h-text_height())/2u,
                        "-", CHARIS_UI_TEXT, FB_TRANSPARENT);
        gfx_draw_string(plus_x+ui_px(11), by+(button_h-text_height())/2u,
                        "+", CHARIS_UI_TEXT, FB_TRANSPARENT);
        u32 value_w = control_x > value_x+ui_px(8)
                    ? control_x-value_x-ui_px(8) : 1;
        fb_set_clip(value_x, by, value_w, button_h);
        gfx_draw_string(value_x, by+(button_h-text_height())/2u,
                        values[i], CHARIS_UI_ACCENT, FB_TRANSPARENT);
        fb_reset_clip();
    }

    u32 footer_y = y+ch-ui_px(34);
    fb_fill_rect(x+rail_w+ui_px(18), footer_y-ui_px(12),
                 cw>rail_w+ui_px(36) ? cw-rail_w-ui_px(36) : 1,
                 ui_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(content_x, footer_y,
                    config_is_persistent() ? "All changes saved"
                                           : "Session only  |  No writable configuration volume",
                    config_is_persistent() ? CHARIS_UI_SUCCESS : CHARIS_UI_WARNING,
                    FB_TRANSPARENT);
}

static bool settings_handle_window_click(window_t* win, int mx, int my) {
    if (!win) return false;
    u32 x=(u32)win->x, y=(u32)win->y;
    u32 cw=(u32)win->width, ch=(u32)win->height+win->titlebar_height;
    u32 title_h=settings_title_height();
    if (mx < (int)x || mx >= (int)(x+cw) ||
        my < (int)y || my >= (int)(y+ch)) return false;
    if (my < (int)(y+title_h)) {
        if (mx >= (int)(x+cw-ui_px(48))) {
            settings_open=false;
            return true;
        }
        return false; /* unclaimed titlebar space is draggable */
    }

    u32 rail_w=ui_px(164);
    if (rail_w > cw/3u) rail_w = cw/3u;
    u32 row_h=settings_row_height_for(ch);
    u32 button_w=settings_button_width();
    u32 plus_x=x+cw-button_w-ui_px(24);
    u32 control_x=plus_x-button_w-ui_px(8);
    u32 first_y=y+title_h+ui_px(82);
    if (mx < (int)(x+rail_w) || my < (int)first_y) return true;
    u32 row=(u32)(my-(int)first_y)/row_h;
    if (row>=3) return true;
    int direction=mx>=(int)plus_x && mx<(int)(plus_x+button_w) ? 1 :
                  mx>=(int)control_x && mx<(int)(control_x+button_w) ? -1 : 0;
    if (!direction) return true;
    if (row==0) {
        int value=config_get_int("font_style",0)+direction;
        if (value<0) value=2;
        if (value>2) value=0;
        config_set_int("font_style",value);
    } else if (row==1) {
        int value=config_get_int("font_scale",1)+direction;
        if (value<1) value=1;
        if (value>3) value=3;
        config_set_int("font_scale",value);
    } else {
        int value=config_get_int("ui_scale",100)+direction*20;
        if (value<80) value=80;
        if (value>120) value=120;
        config_set_int("ui_scale",value);
    }
    gfx_text_apply_preferences();
    config_save(CONFIG_FILE_PATH);
    return true;
}

/* Software cursor. The PS/2 mouse has no hardware cursor once the display is
 * in graphics mode, so the pointer is drawn into the framebuffer. */
void ui_draw_cursor(void) {
    int x = input_get_mouse_x();
    int y = input_get_mouse_y();
    if (x < 0 || y < 0) return;
    if ((u32)x >= g_framebuffer.width || (u32)y >= g_framebuffer.height) return;

    /* 10x16 arrow. */
    static const char SHAPE[16][11] = {
        "X........",
        "XX.......",
        "X.X......",
        "X..X.....",
        "X...X....",
        "X....X...",
        "X.....X..",
        "X......X.",
        "X.......X",
        "X........",
        "X........",
        "X........",
        "X........",
        "X........",
        "X........",
        "X........",
    };

    for (u32 row = 0; row < 16; row++) {
        for (u32 col = 0; col < 10; col++) {
            bool on = SHAPE[row][col] == 'X';
            /* Draw a dark outline so the cursor stays visible on any wallpaper. */
            if (on) fb_put_pixel(x + col, y + row, T_CURSOR);
            else {
                bool edge = (row + 1 < 16 && SHAPE[row + 1][col] == 'X')
                         || (col + 1 < 10 && SHAPE[row][col + 1] == 'X');
                if (edge) fb_put_pixel(x + col, y + row, FB_ARGB(0xFF, 0, 0, 0));
            }
        }
    }
}

static void redraw_all(void) {
    wallpaper_draw((u32)config_get_int("wallpaper", 0));
    desktop_surface_render();
    draw_taskbar();
    wm_render_surfaces();
    draw_menu();
    ui_draw_cursor();
}

/* ── Hit testing ──────────────────────────────────────────────────── */

static int hit_menu_item(int mx, int my) {
    if (!ui.menu_open) return -1;
    u32 h = g_framebuffer.height;
    u32 menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + ui_px(16);
    u32 bar_h = taskbar_height();
    u32 top = h > bar_h + menu_h ? h - bar_h - menu_h : 0;

    if ((u32)mx >= MENU_W) return -1;
    if (my < (int)top + 8) return -1;

    u32 rel = (u32)my - top - 8;
    if (rel >= MENU_ITEM_H * MENU_ITEM_COUNT) return -1;
    return (int)(rel / MENU_ITEM_H);
}

/* ── Input plumbing ───────────────────────────────────────────────── */

static volatile u32 ui_keyboard_hook_events = 0;

u32 ui_keyboard_hook_event_count(void) { return ui_keyboard_hook_events; }

/* Runs in the keyboard IRQ: push the scancode as an event and return. */
static void ui_key_hook(u8 scancode, bool pressed) {
    ui_keyboard_hook_events++;
    input_event_t evt;
    evt.type = pressed ? INPUT_KEY_DOWN : INPUT_KEY_UP;
    evt.key.scancode = scancode;
    evt.key.keycode = 0;
    evt.timestamp = timer_get_ms();
    input_push_event(&evt);
}

void ui_install_input_hooks(void) {
    ui_keyboard_hook_events = 0;
    keyboard_set_key_event_hook(ui_key_hook);
}

static bool super_held = false;

static void shell_close_focused_app(void) {
    window_t* win = wm_get_focused();
    int id = shell_app_id_for_window(win);
    if (id < 0) return;
    switch ((shell_app_id_t)id) {
        case SHELL_APP_TERMINAL: terminal_close(); break;
        case SHELL_APP_FILES: files_close(); break;
        case SHELL_APP_BROWSER: browser_close(); break;
        case SHELL_APP_SETTINGS: settings_open = false; break;
        case SHELL_APP_ABOUT:
            notice_open = false;
            notice_item = -1;
            break;
        default: return;
    }
    shell_sync_app_windows();
    serial_puts("SHELL_WINDOW_CLOSED");
}

static void handle_event(const input_event_t* e) {
    switch (e->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP: {
        u8 code = e->key.scancode;
        bool extended = (code & 0x80u) != 0;
        u8 base = code & 0x7Fu;
        bool pressed = e->type == INPUT_KEY_DOWN;

        /* Alt+Tab is global, but only cycles the bounded set of visible,
         * non-minimized windows exposed by the window manager. */
        if (!extended && base == 0x38u) {
            alt_held = pressed;
            return;
        }
        if (pressed && !extended && base == 0x0Fu && alt_held) {
            (void)wm_focus_next();
            ui.needs_redraw = true;
            return;
        }
        if (pressed && !extended && base == 0x3Eu && alt_held) {
            shell_close_focused_app();
            ui.menu_open = false;
            ui.needs_redraw = true;
            return;
        }

        if (pressed && super_held && extended && base == 0x48u) {
            window_t* win = wm_get_focused();
            ui.menu_open = false;
            if (win) {
                if (win->maximized) {
                    if (wm_restore_window_geometry(win))
                        serial_puts("SHELL_WINDOW_RESTORED");
                } else if (wm_maximize_window(
                               win, g_framebuffer.height - taskbar_height())) {
                    serial_puts("SHELL_WINDOW_MAXIMIZED");
                }
            }
            ui.needs_redraw = true;
            return;
        }
        if (pressed && super_held && extended && base == 0x50u) {
            window_t* win = wm_get_focused();
            ui.menu_open = false;
            if (win) {
                if (win->maximized) {
                    if (wm_restore_window_geometry(win))
                        serial_puts("SHELL_WINDOW_RESTORED");
                } else if (wm_minimize_window(win)) {
                    serial_puts("SHELL_WINDOW_MINIMIZED");
                }
            }
            ui.needs_redraw = true;
            return;
        }

        /* Super is global and toggles the launcher regardless of app focus. */
        if (extended && (base == 0x5Bu || base == 0x5Cu)) {
            super_held = pressed;
            if (pressed) {
                ui.menu_open = !ui.menu_open;
                ui.hover_item = -1;
                ui.needs_redraw = true;
            }
            return;
        }

        if (pressed && !extended && base == 0x57u) {
            ui.menu_open = false;
            notice_open = false;
            notice_item = -1;
            shell_toggle_app(SHELL_APP_SETTINGS);
            ui.needs_redraw = true;
            return;
        }
        if (pressed && !extended && base == 0x44u) {
            ui.menu_open = false;
            notice_open = false;
            shell_open_app(SHELL_APP_FILES);
            ui.needs_redraw = true;
            return;
        }
        if (pressed && !extended && base == 0x58u) {
            ui.menu_open = false;
            notice_open = false;
            shell_open_app(SHELL_APP_TERMINAL);
            ui.needs_redraw = true;
            return;
        }

        /* Escape dismisses shell overlays first. Otherwise the focused app
         * receives it, so Browser can leave address editing without closing. */
        if (pressed && !extended && base == KEY_ESCAPE) {
            if (ui.menu_open) {
                ui.menu_open = false;
                ui.hover_item = -1;
                ui.needs_redraw = true;
                return;
            }
        }

        wm_dispatch_event(e->type, (void*)e);
        ui.needs_redraw = true;
        return;
    }

    case INPUT_MOUSE_MOVE:
        if (shell_resize_window) {
            int id = shell_app_id_for_window(shell_resize_window);
            if (id >= 0) {
                u32 min_width = ui_px(360), min_height = ui_px(220);
                switch ((shell_app_id_t)id) {
                    case SHELL_APP_FILES: min_width=ui_px(540); min_height=ui_px(340); break;
                    case SHELL_APP_TERMINAL: min_width=ui_px(420); min_height=ui_px(280); break;
                    case SHELL_APP_BROWSER: min_width=ui_px(560); min_height=ui_px(340); break;
                    case SHELL_APP_SETTINGS: min_width=ui_px(520); min_height=ui_px(400); break;
                    case SHELL_APP_ABOUT: min_width=ui_px(360); min_height=ui_px(220); break;
                    default: break;
                }
                u32 max_width = g_framebuffer.width > (u32)shell_resize_window->x
                    ? g_framebuffer.width - (u32)shell_resize_window->x : 1u;
                u32 work_bottom = g_framebuffer.height > taskbar_height()
                    ? g_framebuffer.height - taskbar_height() : g_framebuffer.height;
                u32 top = (u32)shell_resize_window->y +
                          shell_resize_window->titlebar_height;
                u32 max_height = work_bottom > top ? work_bottom - top : 1u;
                if (min_width > max_width) min_width = max_width;
                if (min_height > max_height) min_height = max_height;
                int requested_w = shell_resize_start_width +
                                  e->mouse.x - shell_resize_origin_x;
                int requested_h = shell_resize_start_height +
                                  e->mouse.y - shell_resize_origin_y;
                if (requested_w < (int)min_width) requested_w = (int)min_width;
                if (requested_h < (int)min_height) requested_h = (int)min_height;
                if (requested_w > (int)max_width) requested_w = (int)max_width;
                if (requested_h > (int)max_height) requested_h = (int)max_height;
                (void)wm_resize_window(shell_resize_window,
                                       requested_w, requested_h);
            }
        } else if (shell_drag_window) {
            (void)wm_move_window(shell_drag_window,
                                 e->mouse.x - shell_drag_offset_x,
                                 e->mouse.y - shell_drag_offset_y);
        }
        ui.hover_item = hit_menu_item(e->mouse.x, e->mouse.y);
        ui.needs_redraw = true;
        return;

    case INPUT_MOUSE_UP:
        if (e->mouse.button == 1) {
            if (shell_resize_window) {
                serial_printf("SHELL_WINDOW_RESIZED %dx%d\n",
                              shell_resize_window->width,
                              shell_resize_window->height);
            }
            shell_drag_window = NULL;
            shell_resize_window = NULL;
        }
        return;

    case INPUT_MOUSE_DOWN: {
        int mx = e->mouse.x, my = e->mouse.y;
        window_t* hit = wm_get_window_at(mx, my);
        if (hit) {
            wm_focus_window(hit);
            if (e->mouse.button == 1 && shell_app_id_for_window(hit) >= 0 &&
                shell_resize_grip_hit(hit, mx, my)) {
                serial_printf("SHELL_RESIZE_GRIP_ARMED %d,%d\\n", mx, my);
                shell_resize_window = hit;
                shell_drag_window = NULL;
                shell_resize_origin_x = mx;
                shell_resize_origin_y = my;
                shell_resize_start_width = hit->width;
                shell_resize_start_height = hit->height;
                ui.needs_redraw = true;
                return;
            }
            shell_pointer_handled = false;
            wm_dispatch_event(INPUT_MOUSE_DOWN, (void*)e);
            if (e->mouse.button == 1 && shell_app_id_for_window(hit) >= 0 &&
                my >= hit->y &&
                my < hit->y + (int)hit->titlebar_height &&
                !shell_pointer_handled) {
                if (hit->maximized) {
                    (void)wm_restore_window_geometry(hit);
                    shell_drag_offset_x = hit->width / 2;
                    shell_drag_offset_y = (int)hit->titlebar_height / 2;
                } else {
                    shell_drag_offset_x = mx - hit->x;
                    shell_drag_offset_y = my - hit->y;
                }
                shell_drag_window = hit;
            } else {
                shell_drag_window = NULL;
            }
            ui.needs_redraw = true;
            return;
        }
        shell_drag_window = NULL;

        u32 bar_h = taskbar_height();
        u32 bar_top = g_framebuffer.height > bar_h ? g_framebuffer.height - bar_h : 0;
        bool on_taskbar = my >= (int)bar_top;

        if (on_taskbar) {
            int pinned = desktop_surface_taskbar_hit_test(mx, my, bar_top);
            if (pinned == 1) {
                ui.menu_open = false;
                shell_toggle_app(SHELL_APP_FILES);
                ui.needs_redraw = true;
                return;
            }
            if (pinned == 2) {
                ui.menu_open = false;
                shell_toggle_app(SHELL_APP_TERMINAL);
                ui.needs_redraw = true;
                return;
            }
            if (pinned == 3) {
                ui.menu_open = false;
                shell_toggle_app(SHELL_APP_SETTINGS);
                ui.needs_redraw = true;
                return;
            }
            if (pinned == 4) {
                ui.menu_open = false;
                shell_toggle_app(SHELL_APP_BROWSER);
                ui.needs_redraw = true;
                return;
            }
        }

        if (on_taskbar && mx < (int)START_BTN_W) {
            ui.menu_open = !ui.menu_open;
            ui.hover_item = -1;
            ui.needs_redraw = true;
            return;
        }

        int shortcut = desktop_surface_hit_test(mx, my);
        if (shortcut == 0) {
            ui.menu_open = false;
            shell_open_app(SHELL_APP_FILES);
            ui.needs_redraw = true;
            return;
        }

        int menu_hit = hit_menu_item(mx, my);
        if (menu_hit >= 0) {
            ui.menu_open = false;
            ui.hover_item = -1;
            if (menu_hit == 0) {
                shell_open_app(SHELL_APP_TERMINAL);
            } else if (menu_hit == 1) {
                shell_open_app(SHELL_APP_FILES);
            } else if (menu_hit == 2) {
                shell_open_app(SHELL_APP_SETTINGS);
            } else if (menu_hit == 3) {
                shell_open_app(SHELL_APP_BROWSER);
            } else {
                shell_open_app(SHELL_APP_ABOUT);
            }
            ui.needs_redraw = true;
        }
        return;
    }

    default:
        return;
    }
}

/* ── Main loop ────────────────────────────────────────────────────── */

void shell_ui_main(void* arg) {
    (void)arg;

    /* First run: collect the choices an install would normally ask for, then
     * hand over to the desktop. Skipped entirely once setup is done, and
     * skipped on a text-only build where there is nothing to draw it on. */
    if (wizard_is_first_boot()) {
        wizard_run();
    }

    u32 last_w = 0, last_h = 0;

    for (;;) {
        /* Feed the queue: keyboard scancodes arrive via the IRQ hook, mouse
         * state via polling here. */
        input_process_events();

        input_event_t e;
        bool had_event = false;
        while (input_pop_event(&e)) {
            handle_event(&e);
            had_event = true;
        }
        char key;
        bool terminal_has_focus =
            shell_app_windows[SHELL_APP_TERMINAL] &&
            wm_get_focused() == shell_app_windows[SHELL_APP_TERMINAL] &&
            !shell_app_windows[SHELL_APP_TERMINAL]->minimized;
        while (keyboard_get_key(&key)) {
            if (terminal_has_focus) terminal_handle_char(key);
            had_event = true;
        }
        if (had_event) ui.needs_redraw = true;
        shell_sync_app_windows();

        /* Cursor blink. */
        u64 now = timer_get_ms();
        if (now >= ui.next_blink_ms) {
            ui.cursor_blink = !ui.cursor_blink;
            ui.next_blink_ms = now + CURSOR_BLINK;
        }

        /* Redraw on a change, or when the mode geometry moved underneath us. */
        if (g_framebuffer.width != last_w || g_framebuffer.height != last_h) {
            last_w = g_framebuffer.width;
            last_h = g_framebuffer.height;
            mouse_set_bounds(last_w, last_h);
            ui.needs_redraw = true;
        }
        if (ui.needs_redraw) {
            redraw_all();
            ui.needs_redraw = false;
        }
        /* The cursor moves on every mouse packet, so it is drawn every pass
         * rather than waiting for a redraw decision. */
        ui_draw_cursor();

        /* The scheduler is cooperative: yield after each frame so a busy
         * desktop cannot monopolize the CPU while waiting for input. */
        scheduler_yield();
        asm volatile("hlt");
    }
}
