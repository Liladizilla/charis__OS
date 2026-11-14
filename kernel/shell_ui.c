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
#include <kernel/cursor.h>
#include <kernel/browser.h>
#include <kernel/calculator_ui.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/input.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/timer.h>
#include <kernel/rtc.h>
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
#include <kernel/settings.h>
#include <kernel/net.h>
#include <kernel/audio.h>

/* Native desktop widgets and launcher hit-testing. */
extern void desktop_surface_render(void);
extern int desktop_surface_hit_test(int mx, int my);

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
    static const char* const labels[] = {
        "About CharisOS", "Recent files unavailable",
        "Power controls unavailable", "Search apps..."
    };
    u32 width = 0, measured = 0;
    for (u32 i = 0; i < 4u; i++) {
        gfx_text_size(labels[i], &measured, NULL);
        if (measured > width) width = measured;
    }
    u32 needed = width + ui_px(36), base = ui_px(280);
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
    { "Calculator" },
    { "Settings" },
    { "Browser" },
    { "About CharisOS" },
};

static bool notice_open = false;
static int notice_item = -1;
static bool settings_open = false;
static settings_page_id_t settings_active_page_id = SETTINGS_PAGE_DISPLAY;
static u32 settings_nav_offset;
static int settings_hover_page_index = -1;
static bool settings_font_menu_open;

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
static u64 next_clock_refresh_ms;

/* The shell owns application lifecycle; the WM owns focus, geometry,
 * clipping, z-order and repaint order. Native app renderers paint directly
 * into their assigned clipped framebuffer rectangle. */
typedef enum {
    SHELL_APP_TERMINAL = 0,
    SHELL_APP_FILES,
    SHELL_APP_CALCULATOR,
    SHELL_APP_BROWSER,
    SHELL_APP_SETTINGS,
    SHELL_APP_ABOUT,
    SHELL_APP_COUNT
} shell_app_id_t;

static window_t* shell_app_windows[SHELL_APP_COUNT];
static int desktop_hover_shortcut = -1;
static char menu_search_query[40];
static u32 menu_search_length;
static bool desktop_context_menu_open;
static u32 desktop_context_menu_x, desktop_context_menu_y;
static int desktop_context_hover = -1;
static window_t* shell_drag_window;
static int shell_drag_offset_x, shell_drag_offset_y;
static window_t* shell_resize_window;
static int shell_resize_origin_x, shell_resize_origin_y;
static int shell_resize_start_width, shell_resize_start_height;
static window_t* shell_keyboard_resize_window;
static int shell_keyboard_resize_x, shell_keyboard_resize_y;
static int shell_keyboard_resize_width, shell_keyboard_resize_height;
static bool shell_pointer_handled;
static bool alt_held;

static u32 settings_title_height(void);
static u32 settings_panel_width(void);
static u32 settings_panel_height(void);
static bool settings_reflow_window(window_t* win);
static void draw_settings_window(window_t* win);
static bool settings_handle_window_click(window_t* win, int mx, int my);
static void settings_move_page(int direction);
static void settings_reveal_page(settings_page_id_t id);
static void settings_adjust_wallpaper(int direction);
static void settings_adjust_wallpaper(int direction);
static int settings_nav_hit_page(window_t* win, int mx, int my);
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
        case SHELL_APP_CALCULATOR: return calculator_ui_is_open();
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
        case SHELL_APP_CALCULATOR: return "Calculator";
        case SHELL_APP_BROWSER: return "Browser";
        case SHELL_APP_SETTINGS: return "Settings";
        case SHELL_APP_ABOUT: return "About CharisOS";
        default: return "Application";
    }
}

static u32 shell_app_titlebar_height(shell_app_id_t id) {
    (void)id;
    u32 title_width = 0, title_height = 0;
    gfx_text_size("CharisOS", &title_width, &title_height);
    u32 base = ui_px(32u);
    u32 readable = title_height + ui_px(8u);
    return base > readable ? base : readable;
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

static void shell_window_resize_limits(window_t* win,
                                          int* min_width, int* min_height,
                                          int* max_width, int* max_height) {
    int id = shell_app_id_for_window(win);
    u32 min_w = ui_px(360), min_h = ui_px(220);
    switch ((shell_app_id_t)id) {
        case SHELL_APP_FILES: min_w=ui_px(540); min_h=ui_px(340); break;
        case SHELL_APP_CALCULATOR: min_w=ui_px(360); min_h=ui_px(280); break;
        case SHELL_APP_TERMINAL: min_w=ui_px(420); min_h=ui_px(280); break;
        case SHELL_APP_BROWSER: min_w=ui_px(560); min_h=ui_px(340); break;
        case SHELL_APP_SETTINGS: min_w=ui_px(520); min_h=ui_px(400); break;
        case SHELL_APP_ABOUT: min_w=ui_px(360); min_h=ui_px(220); break;
        default: break;
    }

    u32 available_w = g_framebuffer.width > (u32)win->x
                    ? g_framebuffer.width - (u32)win->x : 1u;
    u32 work_bottom = g_framebuffer.height > taskbar_height()
                    ? g_framebuffer.height - taskbar_height()
                    : g_framebuffer.height;
    u32 top = (u32)win->y + win->titlebar_height;
    u32 available_h = work_bottom > top ? work_bottom - top : 1u;

    if (min_w > available_w) min_w = available_w;
    if (min_h > available_h) min_h = available_h;
    *min_width = (int)min_w;
    *min_height = (int)min_h;
    *max_width = (int)available_w;
    *max_height = (int)available_h;
}

static bool shell_resize_app_window(window_t* win, int width, int height) {
    if (!win || shell_app_id_for_window(win) < 0 ||
        win->maximized) return false;
    int min_w, min_h, max_w, max_h;
    shell_window_resize_limits(win, &min_w, &min_h, &max_w, &max_h);
    return wm_resize_window_bounded(win, width, height,
                                    min_w, min_h, max_w, max_h);
}

static void shell_keyboard_resize_end(bool commit) {
    window_t* win = shell_keyboard_resize_window;
    if (!win) return;
    if (!commit) {
        if (!wm_resize_window(win, shell_keyboard_resize_width,
                              shell_keyboard_resize_height)) {
            serial_puts("SHELL_KEYBOARD_RESIZE_CANCEL_FAILED");
            return;
        }
        (void)wm_move_window(win, shell_keyboard_resize_x,
                             shell_keyboard_resize_y);
    }
    if (commit) {
        serial_printf("SHELL_KEYBOARD_RESIZE_COMMITTED %dx%d\n",
                      win->width, win->height);
    } else {
        serial_puts("SHELL_KEYBOARD_RESIZE_CANCELLED");
    }
    shell_keyboard_resize_window = NULL;
}

static void shell_keyboard_resize_start(void) {
    window_t* win = wm_get_focused();
    if (!win || shell_app_id_for_window(win) < 0 || win->maximized ||
        win->minimized || !win->visible) return;
    shell_keyboard_resize_window = win;
    shell_keyboard_resize_x = win->x;
    shell_keyboard_resize_y = win->y;
    shell_keyboard_resize_width = win->width;
    shell_keyboard_resize_height = win->height;
    serial_printf("SHELL_KEYBOARD_RESIZE_STARTED %dx%d\n",
                  win->width, win->height);
}

static void shell_draw_chrome_button(u32 x, u32 y, u32 width,
                                     u32 height, u32 kind, bool hovered,
                                     u32 ink) {
    fb_fill_rect(x, y, width, height,
                 hovered ? CHARIS_UI_SURFACE_RAISED : CHARIS_UI_PANEL_RAISED);
    fb_draw_rect(x, y, width, height,
                 hovered ? CHARIS_UI_BORDER_STRONG : CHARIS_UI_BORDER);
    u32 cx = x + width / 2u;
    u32 cy = y + height / 2u;
    u32 stroke = ui_px(1u);
    if (kind == 0u) {
        fb_fill_rect(cx - ui_px(6u), cy + ui_px(4u), ui_px(12u), stroke, ink);
    } else if (kind == 1u) {
        fb_draw_rect(cx - ui_px(6u), cy - ui_px(5u),
                     ui_px(12u), ui_px(10u), ink);
    } else {
        for (u32 i = 0; i < ui_px(9u); i++) {
            fb_fill_rect(cx - ui_px(4u) + i, cy - ui_px(4u) + i,
                         stroke, stroke, ink);
            fb_fill_rect(cx + ui_px(4u) - i, cy - ui_px(4u) + i,
                         stroke, stroke, ink);
        }
    }
}

static void shell_draw_window_chrome(const window_t* win) {
    if (!win || !win->visible || win->minimized || win->width <= 0 ||
        !win->titlebar_height) return;

    u32 x = (u32)win->x, y = (u32)win->y;
    u32 width = (u32)win->width, height = win->titlebar_height;
    u32 button_w = ui_px(36u), button_h = ui_px(28u);
    u32 gap = ui_px(2u), margin = ui_px(6u);
    if (width < button_w * 3u + gap * 2u + margin * 2u) return;
    if (height < button_h) button_h = height;

    bool focused = win->focused;
    u32 title_color = focused ? CHARIS_UI_TEXT : CHARIS_UI_MUTED;
    fb_fill_rect(x, y, width, height,
                 focused ? CHARIS_UI_PANEL_RAISED : CHARIS_UI_PANEL);
    if (focused)
        fb_fill_rect(x, y, width, ui_px(2u), CHARIS_UI_ACCENT);
    fb_fill_rect(x, y + height - ui_px(1u), width, ui_px(1u),
                 CHARIS_UI_BORDER);

    u32 close_x = x + width - margin - button_w;
    u32 maximize_x = close_x - gap - button_w;
    u32 minimize_x = maximize_x - gap - button_w;
    u32 button_y = y + (height - button_h) / 2u;
    int mx = input_get_mouse_x(), my = input_get_mouse_y();
    bool over_min = mx >= (int)minimize_x &&
                    mx < (int)(minimize_x + button_w) &&
                    my >= (int)button_y && my < (int)(button_y + button_h);
    bool over_max = mx >= (int)maximize_x &&
                    mx < (int)(maximize_x + button_w) &&
                    my >= (int)button_y && my < (int)(button_y + button_h);
    bool over_close = mx >= (int)close_x &&
                      mx < (int)(close_x + button_w) &&
                      my >= (int)button_y && my < (int)(button_y + button_h);

    u32 title_x = x + ui_px(14u);
    u32 title_right = minimize_x > ui_px(10u)
                    ? minimize_x - ui_px(10u) : title_x;
    fb_set_clip(title_x, y,
                title_right > title_x ? title_right - title_x : 1u,
                height);
    gfx_draw_string(title_x,
                    y + (height > text_height()
                       ? (height - text_height()) / 2u : 0u),
                    win->title, title_color, FB_TRANSPARENT);
    fb_reset_clip();

    shell_draw_chrome_button(minimize_x, button_y, button_w, button_h,
                             0u, over_min, title_color);
    shell_draw_chrome_button(maximize_x, button_y, button_w, button_h,
                             1u, over_max, title_color);
    shell_draw_chrome_button(close_x, button_y, button_w, button_h,
                             2u, over_close,
                             focused ? CHARIS_UI_DANGER : CHARIS_UI_MUTED);
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
        case SHELL_APP_CALCULATOR:
            calculator_ui_set_bounds((u32)win->x, (u32)win->y,
                                     (u32)win->width, full_height);
            calculator_ui_render();
            break;
        case SHELL_APP_BROWSER:
            browser_set_bounds((u32)win->x,
                               (u32)win->y + win->titlebar_height,
                               (u32)win->width, (u32)win->height);
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
    shell_draw_window_chrome(win);
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
            case SHELL_APP_CALCULATOR:
                shell_pointer_handled = calculator_ui_handle_mouse(
                    evt->mouse.x, evt->mouse.y);
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
        case SHELL_APP_CALCULATOR:
            (void)calculator_ui_handle_key(base, pressed);
            break;
        case SHELL_APP_BROWSER:
            (void)browser_handle_key(base, pressed);
            break;
        case SHELL_APP_SETTINGS:
            if (pressed && extended && base == 0x48u) {
                settings_move_page(-1);
            } else if (pressed && extended && base == 0x50u) {
                settings_move_page(1);
            } else if (pressed && extended && base == 0x4Bu &&
                       settings_active_page_id == SETTINGS_PAGE_APPEARANCE) {
                settings_adjust_wallpaper(-1);
            } else if (pressed && extended && base == 0x4Du &&
                       settings_active_page_id == SETTINGS_PAGE_APPEARANCE) {
                settings_adjust_wallpaper(1);
            } else if (pressed && !extended && base == KEY_ESCAPE) {
                if (settings_font_menu_open) settings_font_menu_open = false;
                else settings_open = false;
            } else if (pressed && !extended && base == 0x40u) {
                int value = config_get_int("font_scale", 1) + 1;
                if (value > 3) value = 3;
                config_set_int("font_scale", value);
                gfx_text_apply_preferences();
                (void)settings_reflow_window(win);
                config_save(CONFIG_FILE_PATH);
            } else if (pressed && !extended && base == 0x41u) {
                int value = config_get_int("ui_scale", 100) + 20;
                if (value > 120) value = 120;
                config_set_int("ui_scale", value);
                gfx_text_apply_preferences();
                (void)settings_reflow_window(win);
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
        case SHELL_APP_CALCULATOR:
            width = ui_px(440);
            height = ui_px(500);
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
        case SHELL_APP_CALCULATOR:
            x -= (s32)ui_px(24);
            y -= (s32)ui_px(12);
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
                if (shell_keyboard_resize_window == shell_app_windows[i])
                    shell_keyboard_resize_window = NULL;
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
    /* Enum values can arrive from future callers or corrupted dispatch state;
     * validate before indexing the fixed application-window table. */
    if ((u32)id >= (u32)SHELL_APP_COUNT) {
        serial_printf("SHELL_APP_OPEN_REJECTED id=%u\n", (u32)id);
        return;
    }
    if (shell_app_windows[id]) {
        window_t* win = shell_app_windows[id];
        if (win->minimized) (void)wm_restore_window(win);
        else wm_focus_window(win);
        return;
    }
    switch (id) {
        case SHELL_APP_TERMINAL: terminal_open(); break;
        case SHELL_APP_FILES: files_open(); break;
        case SHELL_APP_CALCULATOR: calculator_ui_open(); break;
        case SHELL_APP_BROWSER: browser_open(); break;
        case SHELL_APP_SETTINGS: settings_open = true; break;
        case SHELL_APP_ABOUT:
            notice_item = 4;
            notice_open = true;
            break;
        default: break;
    }
    shell_sync_app_windows();
    if (shell_app_windows[id]) {
        wm_focus_window(shell_app_windows[id]);
        serial_printf("SHELL_APP_OPEN %s\n", shell_app_title(id));
    }
}

static void shell_toggle_app(shell_app_id_t id) {
    if ((u32)id >= (u32)SHELL_APP_COUNT) {
        serial_printf("SHELL_APP_TOGGLE_REJECTED id=%u\n", (u32)id);
        return;
    }
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

static void clock_append_two(char** cursor, u32 value) {
    *(*cursor)++ = (char)('0' + ((value / 10u) % 10u));
    *(*cursor)++ = (char)('0' + (value % 10u));
}

static void draw_taskbar_tray(u32 x, u32 y) {
    net_interface_t* nic = net_get_interface();
    bool network_ready = nic && nic->initialized;
    bool hda_ready = audio_hda_available();
    u32 network_color = network_ready ? CHARIS_UI_SUCCESS : CHARIS_UI_MUTED;
    u32 audio_color = hda_ready ? CHARIS_UI_SUCCESS : CHARIS_UI_MUTED;

    /* Network adapter readiness, not an assertion of Internet connectivity. */
    fb_fill_rect(x + ui_px(8u), y + ui_px(2u), ui_px(4u), ui_px(4u),
                 network_color);
    fb_fill_rect(x + ui_px(2u), y + ui_px(12u), ui_px(5u), ui_px(4u),
                 network_color);
    fb_fill_rect(x + ui_px(13u), y + ui_px(12u), ui_px(5u), ui_px(4u),
                 network_color);
    fb_fill_rect(x + ui_px(8u), y + ui_px(7u), ui_px(3u), ui_px(2u),
                 network_color);
    fb_fill_rect(x + ui_px(5u), y + ui_px(9u), ui_px(2u), ui_px(3u),
                 network_color);
    fb_fill_rect(x + ui_px(12u), y + ui_px(9u), ui_px(2u), ui_px(3u),
                 network_color);

    u32 sx = x + ui_px(28u), sy = y + ui_px(3u);
    fb_fill_rect(sx, sy + ui_px(5u), ui_px(4u), ui_px(8u), audio_color);
    fb_fill_rect(sx + ui_px(4u), sy + ui_px(2u), ui_px(4u), ui_px(14u),
                 audio_color);
    if (hda_ready) {
        fb_fill_rect(sx + ui_px(10u), sy + ui_px(3u), ui_px(2u), ui_px(2u),
                     audio_color);
        fb_fill_rect(sx + ui_px(12u), sy + ui_px(6u), ui_px(2u), ui_px(6u),
                     audio_color);
        fb_fill_rect(sx + ui_px(10u), sy + ui_px(13u), ui_px(2u), ui_px(2u),
                     audio_color);
    }
}

static void draw_taskbar_clock(u32 y, u32 bar_h, u32 screen_w) {
    rtc_datetime_t now;
    char label[6];
    char* cursor = label;
    if (rtc_get_datetime(&now) && now.valid) {
        clock_append_two(&cursor, now.hour);
        *cursor++ = ':';
        clock_append_two(&cursor, now.minute);
    } else {
        *cursor++ = '-'; *cursor++ = '-';
        *cursor++ = ':'; *cursor++ = '-'; *cursor++ = '-';
    }
    *cursor = 0;

    u32 width = 0;
    gfx_text_size(label, &width, NULL);
    u32 right_pad = ui_px(18u);
    if (screen_w <= width + right_pad) return;
    u32 x = screen_w - width - right_pad;
    u32 tray_w = ui_px(48u);
    if (x > tray_w + ui_px(10u))
        draw_taskbar_tray(x - tray_w, y + (bar_h > ui_px(22u)
                         ? (bar_h - ui_px(22u)) / 2u : 0u));
    fb_fill_rect(x - tray_w - ui_px(6u), y + ui_px(10u), ui_px(1u),
                 bar_h > ui_px(20u) ? bar_h - ui_px(20u) : 1u,
                 CHARIS_UI_ACCENT);
    gfx_draw_string(x, y + (bar_h > text_height()
                     ? (bar_h - text_height()) / 2u : 0u),
                    label, CHARIS_UI_TEXT, FB_TRANSPARENT);
}

/* Every launcher entry gets a small, high-contrast icon drawn from
 * primitives. This avoids a font/icon asset dependency in the kernel. */
static void draw_app_icon(u32 app, u32 x, u32 y) {
    u32 ink = app == 1u ? 0xFF91D7A3u :
              app == 2u ? 0xFFF0C674u :
              app == 3u ? 0xFF7EB6FFu :
              app == 4u ? 0xFFB7A0F5u :
              app == 5u ? 0xFF8FC7D8u : CHARIS_UI_TEXT;
    fb_fill_rect(x, y, 20, 20, CHARIS_UI_PANEL_RAISED);
    fb_draw_rect(x, y, 20, 20, CHARIS_UI_BORDER);
    if (app == 1u) {
        gfx_draw_string(x + 2, y + 4, ">_", ink, FB_TRANSPARENT);
    } else if (app == 2u) {
        fb_fill_rect(x + 3, y + 5, 7, 3, ink);
        fb_fill_rect(x + 3, y + 7, 14, 10, ink);
        fb_draw_rect(x + 3, y + 7, 14, 10, CHARIS_UI_BORDER);
    } else if (app == 3u) {
        fb_draw_rect(x + 4, y + 2, 12, 16, ink);
        fb_fill_rect(x + 6, y + 4, 8, 3, CHARIS_UI_PANEL);
        fb_fill_rect(x + 6, y + 9, 3, 3, ink);
        fb_fill_rect(x + 11, y + 9, 3, 3, ink);
        fb_fill_rect(x + 6, y + 14, 3, 2, ink);
        fb_fill_rect(x + 11, y + 14, 3, 2, ink);
    } else if (app == 4u) {
        fb_draw_rect(x + 4, y + 4, 12, 12, ink);
        fb_fill_rect(x + 8, y + 1, 4, 18, ink);
        fb_fill_rect(x + 1, y + 8, 18, 4, ink);
        fb_fill_rect(x + 7, y + 7, 6, 6, CHARIS_UI_PANEL_RAISED);
    } else if (app == 5u) {
        fb_draw_rect(x + 3, y + 3, 14, 14, ink);
        fb_fill_rect(x + 4, y + 7, 12, 1, ink);
        fb_fill_rect(x + 4, y + 12, 12, 1, ink);
        fb_fill_rect(x + 9, y + 4, 1, 12, ink);
    } else {
        fb_draw_rect(x + 5, y + 2, 10, 16, ink);
        gfx_draw_string(x + 9, y + 4, "i", ink, FB_TRANSPARENT);
    }
}

static const shell_app_id_t TASKBAR_APPS[] = {
    SHELL_APP_FILES, SHELL_APP_TERMINAL, SHELL_APP_CALCULATOR,
    SHELL_APP_SETTINGS, SHELL_APP_BROWSER
};
static const u32 TASKBAR_ICON_IDS[] = { 2u, 1u, 3u, 4u, 5u };
#define TASKBAR_APP_COUNT ((u32)(sizeof(TASKBAR_APPS) / sizeof(TASKBAR_APPS[0])))

static u32 taskbar_running_count(void) {
    u32 count = 0;
    for (u32 i = 0; i < TASKBAR_APP_COUNT; i++)
        if (shell_app_windows[TASKBAR_APPS[i]]) count++;
    return count;
}

static u32 taskbar_slot_width(u32 screen_width, u32 count) {
    if (!count) return 0;
    u32 start = START_BTN_W + ui_px(8u);
    u32 clock_lane = ui_px(112u), right_pad = ui_px(18u);
    u32 gaps = ui_px(4u) * (count - 1u);
    u32 available = screen_width > start + clock_lane + right_pad + gaps
                  ? screen_width - start - clock_lane - right_pad - gaps : 0u;
    u32 slot = available / count;
    u32 maximum = ui_px(156u);
    if (slot > maximum) slot = maximum;
    return slot;
}

static int taskbar_app_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    u32 count = taskbar_running_count();
    if (!count) return -1;
    u32 slot = taskbar_slot_width(g_framebuffer.width, count);
    u32 gap = ui_px(4u), seen = 0;
    u32 start = START_BTN_W + ui_px(8u);
    for (u32 i = 0; i < TASKBAR_APP_COUNT; i++) {
        shell_app_id_t id = TASKBAR_APPS[i];
        if (!shell_app_windows[id]) continue;
        u32 x = start + seen * (slot + gap);
        if (mx >= (int)x && mx < (int)(x + slot)) return (int)id;
        seen++;
    }
    return -1;
}

static void draw_taskbar(void) {
    u32 width = g_framebuffer.width, height = g_framebuffer.height;
    u32 bar_h = taskbar_height();
    u32 y = height > bar_h ? height - bar_h : 0u;
    fb_fill_rect(0, y, width, bar_h, T_TASKBAR);
    fb_fill_rect(0, y, width, ui_px(1u), T_BORDER);

    int mx = input_get_mouse_x(), my = input_get_mouse_y();
    bool start_hover = my >= (int)y && mx >= 0 && mx < (int)START_BTN_W;
    u32 start_bg = ui.menu_open ? T_MENU_HOVER
                    : start_hover ? T_START_HOVER : T_START_BTN;
    fb_fill_rect(0, y, START_BTN_W, bar_h, start_bg);
    fb_draw_rect(0, y, START_BTN_W, bar_h, T_BORDER);

    /* Clean C logo mark, without the nested blue placeholder tile. */
    u32 mark_size = ui_px(24u);
    u32 mark_x = START_BTN_W > mark_size ? (START_BTN_W - mark_size) / 2u : 0u;
    u32 mark_y = y + (bar_h > mark_size ? (bar_h - mark_size) / 2u : 0u);
    fb_fill_rect(mark_x + ui_px(3u), mark_y + ui_px(3u),
                 ui_px(18u), ui_px(18u), CHARIS_UI_ACCENT);
    fb_fill_rect(mark_x + ui_px(8u), mark_y + ui_px(7u),
                 ui_px(13u), ui_px(10u), start_bg);
    fb_fill_rect(mark_x + ui_px(15u), mark_y + ui_px(9u),
                 ui_px(7u), ui_px(6u), start_bg);

    u32 count = taskbar_running_count();
    u32 slot = taskbar_slot_width(width, count);
    u32 gap = ui_px(4u), seen = 0;
    u32 button_h = bar_h > ui_px(8u) ? bar_h - ui_px(8u) : bar_h;
    u32 button_y = y + (bar_h > button_h ? (bar_h - button_h) / 2u : 0u);
    u32 start = START_BTN_W + ui_px(8u);
    for (u32 i = 0; i < TASKBAR_APP_COUNT; i++) {
        shell_app_id_t id = TASKBAR_APPS[i];
        window_t* win = shell_app_windows[id];
        if (!win) continue;
        u32 x = start + seen * (slot + gap);
        bool focused = win->focused && !win->minimized;
        bool hover = mx >= (int)x && mx < (int)(x + slot) &&
                     my >= (int)y && my < (int)(y + bar_h);
        if (focused || hover) {
            fb_fill_rect(x, button_y, slot, button_h,
                         focused ? CHARIS_UI_ACCENT_SURFACE : T_START_HOVER);
            fb_draw_rect(x, button_y, slot, button_h,
                         focused ? CHARIS_UI_BORDER_STRONG : T_BORDER);
        }
        u32 icon_size = ui_px(20u);
        u32 icon_y = y + (bar_h > icon_size ? (bar_h - icon_size) / 2u : 0u);
        if (slot >= ui_px(88u)) {
            draw_app_icon(TASKBAR_ICON_IDS[i], x + ui_px(7u), icon_y);
            u32 title_x = x + ui_px(34u);
            u32 title_width = slot > ui_px(42u) ? slot - ui_px(42u) : 1u;
            fb_set_clip(title_x, button_y, title_width, button_h);
            gfx_draw_string(title_x,
                            button_y + (button_h > text_height()
                                     ? (button_h - text_height()) / 2u : 0u),
                            shell_app_title(id),
                            focused ? CHARIS_UI_TEXT : CHARIS_UI_MUTED,
                            FB_TRANSPARENT);
            fb_reset_clip();
        } else {
            draw_app_icon(TASKBAR_ICON_IDS[i],
                          x + (slot > icon_size ? (slot - icon_size) / 2u : 0u),
                          icon_y);
        }
        fb_fill_rect(x + ui_px(6u), y + bar_h - ui_px(3u),
                     slot > ui_px(12u) ? slot - ui_px(12u) : slot,
                     ui_px(2u), win->minimized ? CHARIS_UI_MUTED : CHARIS_UI_ACCENT);
        seen++;
    }
    draw_taskbar_clock(y, bar_h, width);
}

static char menu_search_lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
}

static bool menu_label_matches(const char* label, const char* query) {
    if (!label || !query || !*query) return true;
    for (const char* start = label; *start; start++) {
        const char* a = start;
        const char* b = query;
        while (*a && *b && menu_search_lower(*a) == menu_search_lower(*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

static shell_app_id_t menu_app_at(u32 index) {
    static const shell_app_id_t apps[MENU_ITEM_COUNT] = {
        SHELL_APP_TERMINAL, SHELL_APP_FILES, SHELL_APP_CALCULATOR,
        SHELL_APP_SETTINGS, SHELL_APP_BROWSER, SHELL_APP_ABOUT
    };
    return index < MENU_ITEM_COUNT ? apps[index] : SHELL_APP_COUNT;
}

static u32 menu_build_results(int* results, u32 capacity) {
    if (!results || !capacity) return 0;
    u32 count = 0;
    for (u32 i = 0; i < MENU_ITEM_COUNT && count < capacity; i++) {
        if (menu_label_matches(MENU_ITEMS[i].label, menu_search_query))
            results[count++] = (int)i;
    }
    return count;
}

static u32 menu_height_for_count(u32 count) {
    u32 rows = count ? count : 1u;
    u32 footer = text_height() * 2u + ui_px(16u);
    return ui_px(52u) + rows * MENU_ITEM_H + footer;
}

static void shell_menu_open(void) {
    ui.menu_open = true;
    ui.hover_item = -1;
    menu_search_query[0] = 0;
    menu_search_length = 0;
    ui.needs_redraw = true;
}

static void shell_menu_toggle(void) {
    if (ui.menu_open) {
        ui.menu_open = false;
        ui.hover_item = -1;
    } else {
        shell_menu_open();
    }
    ui.needs_redraw = true;
}

static void shell_menu_select_index(int index) {
    if (index < 0 || (u32)index >= MENU_ITEM_COUNT) return;
    ui.menu_open = false;
    ui.hover_item = -1;
    shell_open_app(menu_app_at((u32)index));
    ui.needs_redraw = true;
}

static void shell_menu_handle_character(char key) {
    if (!ui.menu_open) return;
    if (key == '\b') {
        if (menu_search_length) menu_search_query[--menu_search_length] = 0;
        ui.needs_redraw = true;
        return;
    }
    if (key == '\n' || key == '\r') {
        int results[MENU_ITEM_COUNT];
        u32 count = menu_build_results(results, MENU_ITEM_COUNT);
        if (count) shell_menu_select_index(results[0]);
        return;
    }
    if (key >= 32 && key <= 126 &&
        menu_search_length + 1u < sizeof(menu_search_query)) {
        menu_search_query[menu_search_length++] = key;
        menu_search_query[menu_search_length] = 0;
        ui.needs_redraw = true;
    }
}

static const char* const DESKTOP_CONTEXT_ITEMS[] = {
    "New folder (read-only)",
    "Change wallpaper",
    "Display settings",
    "About CharisOS"
};
#define DESKTOP_CONTEXT_COUNT ((u32)(sizeof(DESKTOP_CONTEXT_ITEMS) / sizeof(DESKTOP_CONTEXT_ITEMS[0])))

static u32 desktop_context_menu_width(void) {
    u32 width = 0, measured = 0;
    for (u32 i = 0; i < DESKTOP_CONTEXT_COUNT; i++) {
        gfx_text_size(DESKTOP_CONTEXT_ITEMS[i], &measured, NULL);
        if (measured > width) width = measured;
    }
    return width + ui_px(34u);
}

static u32 desktop_context_menu_height(void) {
    return DESKTOP_CONTEXT_COUNT * MENU_ITEM_H + ui_px(12u);
}

static void desktop_context_menu_open_at(int mx, int my) {
    u32 width = desktop_context_menu_width();
    u32 height = desktop_context_menu_height();
    u32 screen_w = g_framebuffer.width;
    u32 work_bottom = g_framebuffer.height > taskbar_height()
                    ? g_framebuffer.height - taskbar_height()
                    : g_framebuffer.height;
    desktop_context_menu_x = mx < 0 ? 0u : (u32)mx;
    desktop_context_menu_y = my < 0 ? 0u : (u32)my;
    if (desktop_context_menu_x + width > screen_w)
        desktop_context_menu_x = screen_w > width ? screen_w - width : 0u;
    if (desktop_context_menu_y + height > work_bottom)
        desktop_context_menu_y = work_bottom > height
                               ? work_bottom - height : 0u;
    desktop_context_hover = -1;
    desktop_context_menu_open = true;
    ui.menu_open = false;
    ui.hover_item = -1;
    ui.needs_redraw = true;
}

static bool desktop_context_menu_contains(int mx, int my) {
    if (!desktop_context_menu_open) return false;
    u32 width = desktop_context_menu_width();
    u32 height = desktop_context_menu_height();
    return mx >= (int)desktop_context_menu_x &&
           mx < (int)(desktop_context_menu_x + width) &&
           my >= (int)desktop_context_menu_y &&
           my < (int)(desktop_context_menu_y + height);
}

static int desktop_context_menu_hit(int mx, int my) {
    if (!desktop_context_menu_contains(mx, my)) return -1;
    u32 row = (u32)(my - (int)desktop_context_menu_y) / MENU_ITEM_H;
    return row < DESKTOP_CONTEXT_COUNT ? (int)row : -1;
}

static void desktop_context_menu_select(int index) {
    if (index < 0 || (u32)index >= DESKTOP_CONTEXT_COUNT) return;
    switch (index) {
        case 0:
            /* Visible as disabled: filesystem write support is not present. */
            serial_puts("SHELL_CONTEXT_NEW_FOLDER_UNAVAILABLE_READ_ONLY");
            return;
        case 1:
            shell_open_app(SHELL_APP_SETTINGS);
            settings_select_page(SETTINGS_PAGE_APPEARANCE);
            break;
        case 2:
            shell_open_app(SHELL_APP_SETTINGS);
            settings_select_page(SETTINGS_PAGE_DISPLAY);
            break;
        case 3:
            shell_open_app(SHELL_APP_ABOUT);
            break;
        default:
            return;
    }
    desktop_context_menu_open = false;
    desktop_context_hover = -1;
    ui.needs_redraw = true;
}

static void draw_desktop_context_menu(void) {
    if (!desktop_context_menu_open) return;
    u32 x = desktop_context_menu_x;
    u32 y = desktop_context_menu_y;
    u32 width = desktop_context_menu_width();
    u32 height = desktop_context_menu_height();

    fb_fill_rect(x + ui_px(4u), y + ui_px(4u), width, height,
                 CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, width, height, CHARIS_UI_PANEL_RAISED);
    fb_draw_rect(x, y, width, height, CHARIS_UI_BORDER_STRONG);
    for (u32 i = 0; i < DESKTOP_CONTEXT_COUNT; i++) {
        u32 row_y = y + ui_px(6u) + i * MENU_ITEM_H;
        bool disabled = i == 0u;
        bool hovered = desktop_context_hover == (int)i && !disabled;
        fb_fill_rect(x + ui_px(2u), row_y, width - ui_px(4u), MENU_ITEM_H,
                     hovered ? CHARIS_UI_ACCENT_SURFACE : CHARIS_UI_PANEL_RAISED);
        if (hovered)
            fb_fill_rect(x + ui_px(2u), row_y, ui_px(2u), MENU_ITEM_H,
                         CHARIS_UI_ACCENT);
        fb_set_clip(x + ui_px(12u), row_y,
                    width > ui_px(24u) ? width - ui_px(24u) : 1u,
                    MENU_ITEM_H);
        gfx_draw_string(x + ui_px(14u),
                        row_y + (MENU_ITEM_H > text_height()
                               ? (MENU_ITEM_H - text_height()) / 2u : 0u),
                        DESKTOP_CONTEXT_ITEMS[i],
                        disabled ? CHARIS_UI_MUTED :
                        hovered ? CHARIS_UI_TEXT : CHARIS_UI_TEXT,
                        FB_TRANSPARENT);
        fb_reset_clip();
    }
}

static void draw_menu(void) {
    if (!ui.menu_open) return;

    int results[MENU_ITEM_COUNT];
    u32 count = menu_build_results(results, MENU_ITEM_COUNT);
    u32 rows = count ? count : 1u;
    u32 h = g_framebuffer.height;
    u32 menu_h = menu_height_for_count(count);
    u32 bar_h = taskbar_height();
    u32 y = h > bar_h + menu_h ? h - bar_h - menu_h : 0;
    u32 x = 0;

    fb_fill_rect(x + ui_px(4u), y + ui_px(4u), MENU_W, menu_h,
                 CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, MENU_W, menu_h, T_MENU);
    fb_draw_rect(x, y, MENU_W, menu_h, T_BORDER);

    u32 search_x = x + ui_px(10u), search_y = y + ui_px(10u);
    u32 search_w = MENU_W - ui_px(20u), search_h = ui_px(32u);
    fb_fill_rect(search_x, search_y, search_w, search_h,
                 CHARIS_UI_SURFACE);
    fb_draw_rect(search_x, search_y, search_w, search_h,
                 CHARIS_UI_BORDER_STRONG);
    const char* shown = menu_search_length ? menu_search_query : "Search apps...";
    u32 shown_w = 0, shown_h = 0;
    gfx_text_size(shown, &shown_w, &shown_h);
    u32 shown_y = search_y + (search_h > shown_h
                  ? (search_h - shown_h) / 2u : 0u);
    fb_set_clip(search_x + ui_px(9u), search_y,
                search_w > ui_px(18u) ? search_w - ui_px(18u) : 1u,
                search_h);
    gfx_draw_string(search_x + ui_px(9u), shown_y, shown,
                    menu_search_length ? T_FG : T_FG_DIM, FB_TRANSPARENT);
    fb_reset_clip();

    u32 list_y = y + ui_px(50u);
    if (!count) {
        gfx_draw_string(x + ui_px(16u), list_y + ui_px(7u),
                        "No matching apps", T_FG_DIM, FB_TRANSPARENT);
    } else {
        for (u32 row = 0; row < count; row++) {
            int index = results[row];
            u32 iy = list_y + row * MENU_ITEM_H;
            bool hovered = ui.hover_item == index;
            fb_fill_rect(x + ui_px(2u), iy, MENU_W - ui_px(4u), MENU_ITEM_H,
                         hovered ? T_MENU_HOVER : T_MENU_ITEM);
            draw_app_icon((u32)index + 1u, x + ui_px(12u), iy + ui_px(7u));
            gfx_draw_string(x + ui_px(44u),
                            iy + (MENU_ITEM_H > text_height()
                                ? (MENU_ITEM_H - text_height()) / 2u : 0u),
                            MENU_ITEMS[index].label, T_FG, FB_TRANSPARENT);
        }
    }

    u32 footer_y = list_y + rows * MENU_ITEM_H + ui_px(7u);
    fb_fill_rect(x + ui_px(10u), footer_y - ui_px(3u),
                 MENU_W - ui_px(20u), ui_px(1u), T_BORDER);
    u32 footer_text_h = text_height();
    gfx_draw_string(x + ui_px(14u), footer_y,
                    "Recent files unavailable", T_FG_DIM, FB_TRANSPARENT);
    gfx_draw_string(x + ui_px(14u),
                    footer_y + footer_text_h + ui_px(3u),
                    "Power controls unavailable", T_FG_DIM, FB_TRANSPARENT);
}

/* A launcher item must not silently send output to an invisible VGA console.
 * Until each application has a real framebuffer UI, explain its current state
 * in a consistent, dismissible panel instead of pretending it launched. */

static void draw_about_window(window_t* win) {
    if (!win || !notice_open || notice_item != 4) return;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 w = (u32)win->width;
    u32 h = (u32)win->height + win->titlebar_height;
    u32 title_h = win->titlebar_height;

    fb_fill_rect(x + ui_px(5), y + ui_px(6), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER_STRONG);
    gfx_draw_string(x + ui_px(24), y + title_h + ui_px(34),
                    NOTICE_LINE_1[4], CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + ui_px(24), y + title_h + ui_px(64),
                    NOTICE_LINE_2[4], CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + ui_px(24), y + title_h + ui_px(108),
                    "Core services and desktop applications remain under active development.",
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
    if (my < (int)(y + title_h))
        return false; /* Shared window chrome and drag handling own the titlebar. */
    return true;
}

/* Native Settings application: navigation rail plus a focused settings page. */
static u32 settings_title_height(void) {
    u32 minimum = ui_px(32);
    u32 readable = text_height() + ui_px(8);
    return minimum > readable ? minimum : readable;
}
static u32 settings_row_height(void) {
    u32 minimum = ui_px(100);
    u32 readable = text_height() * 2u + ui_px(28);
    return minimum > readable ? minimum : readable;
}
static u32 settings_footer_reserved(void) {
    return ui_px(24) + text_height() + ui_px(12);
}
static u32 settings_button_width(void) {
    u32 width = ui_px(32), glyph_width = 0;
    gfx_text_size("+", &glyph_width, NULL);
    if (width < glyph_width + ui_px(8)) width = glyph_width + ui_px(8);
    return width;
}
static u32 settings_button_height(void) {
    u32 height = ui_px(34), readable = text_height() + ui_px(8);
    return height > readable ? height : readable;
}
static u32 settings_nav_height(void) {
    u32 height = text_height() + ui_px(2);
    u32 minimum = ui_px(24);
    return height > minimum ? height : minimum;
}
static u32 settings_nav_step(void) {
    return settings_nav_height() + ui_px(2);
}
static u32 settings_nav_top_offset(void) {
    return settings_title_height() + text_height() + ui_px(18);
}
static u32 settings_nav_bottom_offset(void) {
    return settings_footer_reserved() + ui_px(12);
}
static u32 settings_nav_visible_rows(const window_t* win) {
    if (!win) return (u32)settings_catalog_count();
    u32 total = (u32)win->height + win->titlebar_height;
    u32 fixed = settings_nav_top_offset() + settings_nav_bottom_offset();
    if (total <= fixed) return 1u;
    u32 rows = (total - fixed) / settings_nav_step();
    return rows ? rows : 1u;
}
static const settings_page_t* settings_active_page(void) {
    const settings_page_t* page = settings_catalog_find(settings_active_page_id);
    if (page) return page;
    settings_active_page_id = SETTINGS_PAGE_DISPLAY;
    return settings_catalog_find(settings_active_page_id);
}
static void settings_select_page(settings_page_id_t id) {
    if (!settings_catalog_find(id) || settings_active_page_id == id) return;
    settings_font_menu_open = false;
    settings_active_page_id = id;
    settings_reveal_page(id);
    serial_printf("SHELL_SETTINGS_PAGE %u\n", (u32)id);
}

static void settings_move_page(int direction) {
    usize count = settings_catalog_count();
    if (!count || !direction) return;

    usize current = 0;
    bool found = false;
    for (usize i = 0; i < count; i++) {
        const settings_page_t* page = settings_catalog_at(i);
        if (page && page->id == settings_active_page_id) {
            current = i;
            found = true;
            break;
        }
    }
    if (!found) current = 0;
    else if (direction < 0) current = current ? current - 1u : count - 1u;
    else current = (current + 1u) % count;

    const settings_page_t* page = settings_catalog_at(current);
    if (page) settings_select_page(page->id);
}

static void settings_adjust_wallpaper(int direction) {
    if (!direction) return;
    u32 count = wallpaper_count();
    if (!count) return;

    int selected = config_get_int("wallpaper", (int)WALLPAPER_CHARIS);
    if (selected < 0 || (u32)selected >= count)
        selected = (int)WALLPAPER_CHARIS;

    int step = direction < 0 ? -1 : 1;
    selected = (selected + (int)count + step) % (int)count;
    config_set_int("wallpaper", selected);
    (void)config_save(CONFIG_FILE_PATH);
    serial_printf("SHELL_WALLPAPER_SELECTED %d\n", selected);
}
static u32 settings_rail_width(void) {
    u32 width = ui_px(164), measured = 0;
    gfx_text_size("PREFERENCES", &measured, NULL);
    if (measured > width) width = measured;
    for (usize i = 0; i < settings_catalog_count(); i++) {
        const settings_page_t* page = settings_catalog_at(i);
        if (!page) continue;
        gfx_text_size(page->title, &measured, NULL);
        if (measured > width) width = measured;
        gfx_text_size(settings_group_label(page->group), &measured, NULL);
        if (measured > width) width = measured;
    }
    width += ui_px(28);
    u32 maximum = settings_panel_width() / 2u;
    return width > maximum ? maximum : width;
}

static void settings_reveal_page(settings_page_id_t id) {
    usize count = settings_catalog_count();
    usize index = count;
    for (usize i = 0; i < count; i++) {
        const settings_page_t* page = settings_catalog_at(i);
        if (page && page->id == id) { index = i; break; }
    }
    if (index >= count) return;

    u32 visible = settings_nav_visible_rows(
        shell_app_windows[SHELL_APP_SETTINGS]);
    if (visible > count) visible = (u32)count;
    if (index < settings_nav_offset)
        settings_nav_offset = (u32)index;
    else if (index >= (usize)settings_nav_offset + visible)
        settings_nav_offset = (u32)index - visible + 1u;

    u32 maximum = count > visible ? (u32)count - visible : 0u;
    if (settings_nav_offset > maximum) settings_nav_offset = maximum;
}

static int settings_nav_hit_page(window_t* win, int mx, int my) {
    if (!settings_open || !win || !win->visible || win->minimized)
        return -1;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 width = (u32)win->width;
    u32 rail = settings_rail_width();
    if (rail > width / 2u) rail = width / 2u;
    if (mx < (int)x || mx >= (int)(x + rail)) return -1;

    u32 top = y + settings_nav_top_offset();
    if (my < (int)top) return -1;
    u32 relative = (u32)(my - (int)top);
    u32 step = settings_nav_step();
    u32 within = relative % step;
    if (within >= settings_nav_height()) return -1;

    usize index = (usize)settings_nav_offset + relative / step;
    if (index >= settings_catalog_count()) return -1;
    return (int)index;
}

static void settings_draw_wrapped_text(u32 x, u32 y, u32 width,
                                       const char* text, u32 color,
                                       u32 max_lines) {
    if (!text || !width || !max_lines) return;
    u32 line_step = text_height() + ui_px(4);
    fb_set_clip(x, y, width, line_step * max_lines);
    const char* cursor = text;
    u32 line_number = 0;

    while (*cursor && line_number < max_lines) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor) break;

        char line[128];
        u32 line_length = 0;
        line[0] = 0;

        while (*cursor && *cursor != '\n') {
            const char* word = cursor;
            u32 word_length = 0;
            while (word[word_length] && word[word_length] != ' ' &&
                   word[word_length] != '\t' && word[word_length] != '\n')
                word_length++;
            if (word_length >= sizeof(line)) word_length = sizeof(line) - 1u;

            char candidate[128];
            if ((usize)line_length + (line_length ? 1u : 0u) +
                (usize)word_length >= sizeof(candidate) && line_length)
                break;

            u32 candidate_length = 0;
            for (u32 i = 0; i < line_length; i++)
                candidate[candidate_length++] = line[i];
            if (candidate_length) candidate[candidate_length++] = ' ';
            for (u32 i = 0; i < word_length; i++)
                candidate[candidate_length++] = word[i];
            candidate[candidate_length] = 0;

            u32 candidate_width = 0;
            gfx_text_size(candidate, &candidate_width, NULL);
            if (candidate_width > width && line_length) break;

            if (candidate_width > width && !line_length) {
                u32 accepted = 0;
                while (accepted < word_length) {
                    char partial[128];
                    for (u32 i = 0; i < accepted; i++) partial[i] = word[i];
                    partial[accepted] = 0;
                    gfx_text_size(partial, &candidate_width, NULL);
                    if (candidate_width > width) break;
                    accepted++;
                }
                if (!accepted) accepted = 1;
                for (u32 i = 0; i < accepted; i++) line[i] = word[i];
                line_length = accepted;
                line[line_length] = 0;
                cursor += accepted;
                break;
            }

            for (u32 i = 0; i < candidate_length; i++)
                line[i] = candidate[i];
            line_length = candidate_length;
            line[line_length] = 0;
            cursor += word_length;
            while (*cursor == ' ' || *cursor == '\t') cursor++;
        }

        if (line_length) {
            gfx_draw_string(x, y + line_number * line_step, line,
                            color, FB_TRANSPARENT);
            line_number++;
        }
        if (*cursor == '\n') cursor++;
        else if (!line_length) break;
    }
    fb_reset_clip();
}

static void settings_draw_navigation(u32 x, u32 y, u32 rail_width,
                                     u32 total_height, u32 title_height,
                                     const settings_page_t* active) {
    u32 rail_x = x + ui_px(1);
    u32 clip_y = y + title_height + ui_px(1);
    u32 clip_h = total_height > title_height + ui_px(2)
               ? total_height - title_height - ui_px(2) : 1u;
    fb_set_clip(rail_x, clip_y, rail_width, clip_h);

    u32 heading_y = y + title_height + ui_px(10);
    gfx_draw_string(x + ui_px(18), heading_y,
                    "PREFERENCES", CHARIS_UI_MUTED, FB_TRANSPARENT);

    u32 nav_top = y + settings_nav_top_offset();
    u32 nav_height = settings_nav_height();
    u32 nav_step = settings_nav_step();
    usize count = settings_catalog_count();
    u32 visible = settings_nav_visible_rows(shell_app_windows[SHELL_APP_SETTINGS]);
    usize end = (usize)settings_nav_offset + visible;
    if (end > count) end = count;

    const settings_page_t* previous = NULL;
    if (settings_nav_offset)
        previous = settings_catalog_at(settings_nav_offset - 1u);
    for (usize i = settings_nav_offset; i < end; i++) {
        const settings_page_t* page = settings_catalog_at(i);
        if (!page) continue;
        u32 row_y = nav_top + (u32)(i - settings_nav_offset) * nav_step;
        if (previous && previous->group != page->group)
            fb_fill_rect(x + ui_px(16), row_y, rail_width - ui_px(32),
                         ui_px(1), CHARIS_UI_BORDER);

        bool selected = active && active->id == page->id;
        bool hovered = settings_hover_page_index == (int)i;
        if (selected || hovered) {
            fb_fill_rect(x + ui_px(8), row_y, rail_width - ui_px(16),
                         nav_height,
                         selected ? CHARIS_UI_ACCENT_SURFACE
                                  : CHARIS_UI_SURFACE_RAISED);
            if (selected)
                fb_fill_rect(x + ui_px(8), row_y, ui_px(3), nav_height,
                             CHARIS_UI_ACCENT);
        }

        fb_set_clip(x + ui_px(18), row_y,
                    rail_width > ui_px(30) ? rail_width - ui_px(30) : 1u,
                    nav_height);
        u32 label_y = row_y + (nav_height > text_height()
                      ? (nav_height - text_height()) / 2u : 0u);
        gfx_draw_string(x + ui_px(22), label_y, page->title,
                        selected ? CHARIS_UI_TEXT :
                        hovered ? CHARIS_UI_TEXT : CHARIS_UI_MUTED,
                        FB_TRANSPARENT);
        fb_reset_clip();
        previous = page;
    }
    fb_reset_clip();
}

static u32 settings_row_height_for(u32 full_height) {
    u32 desired = settings_row_height();
    /* Reserve title, row lead-in and the complete dynamic footer before
     * dividing the remaining space between the three settings rows. */
    u32 fixed = settings_title_height() + ui_px(92) +
                settings_footer_reserved();
    if (full_height <= fixed) return ui_px(64);
    u32 available = (full_height - fixed) / 3u;
    return desired < available ? desired : available;
}
typedef struct {
    u32 row_x, row_y, row_w, row_h;
    u32 label_x, label_y, label_w;
    u32 control_x, control_y, control_w, control_h;
} settings_display_row_layout_t;

static bool settings_display_row_layout(window_t* win, u32 row_index,
                                        settings_display_row_layout_t* out) {
    if (!win || !out || row_index >= 3u) return false;

    u32 x = (u32)win->x, y = (u32)win->y;
    u32 cw = (u32)win->width;
    u32 ch = (u32)win->height + win->titlebar_height;
    u32 rail_w = settings_rail_width();
    if (rail_w > cw / 2u) rail_w = cw / 2u;
    u32 content_x = x + rail_w + ui_px(28u);
    u32 row_h = settings_row_height_for(ch);
    u32 row_y = y + settings_title_height() + ui_px(92u) +
                row_index * row_h;
    u32 row_w = cw > rail_w + ui_px(30u)
              ? cw - rail_w - ui_px(30u) : 1u;

    int font_scale = config_get_int("font_scale", 1);
    if (font_scale < 1 || font_scale > 3) font_scale = 1;
    const char* labels[3] = {
        font_scale >= 3 ? "F" : font_scale > 1 ? "Font" : "Font family",
        font_scale >= 3 ? "T" : font_scale > 1 ? "Text" : "Text size (%)",
        font_scale >= 3 ? "UI" : font_scale > 1 ? "UI" : "UI scale (%)"
    };
    u32 label_column_w = 0, measured = 0;
    for (u32 i = 0; i < 3u; i++) {
        gfx_text_size(labels[i], &measured, NULL);
        if (measured > label_column_w) label_column_w = measured;
    }

    u32 label_x = content_x + ui_px(8u);
    u32 value_x = label_x + label_column_w + ui_px(16u);
    u32 control_right = x + cw > ui_px(24u) ? x + cw - ui_px(24u) : x + cw;
    if (value_x >= control_right) value_x = content_x;

    u32 button_h = settings_button_height();
    u32 control_y = row_y + (row_h > button_h ? (row_h - button_h) / 2u : 0u);
    *out = (settings_display_row_layout_t) {
        .row_x = content_x - ui_px(10u),
        .row_y = row_y,
        .row_w = row_w,
        .row_h = row_h,
        .label_x = label_x,
        .label_y = row_y + (row_h > text_height() + ui_px(8u)
                          ? ui_px(8u) : 0u),
        .label_w = value_x > label_x + ui_px(12u)
                 ? value_x - label_x - ui_px(12u) : 1u,
        .control_x = value_x,
        .control_y = control_y,
        .control_w = control_right > value_x + ui_px(4u)
                   ? control_right - value_x - ui_px(4u) : 1u,
        .control_h = button_h
    };
    return true;
}

static void settings_font_popup_bounds(window_t* win, u32* x, u32* y,
                                       u32* width, u32* height,
                                       u32* option_height) {
    settings_display_row_layout_t layout;
    if (!settings_display_row_layout(win, 0u, &layout)) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = 1;
        if (height) *height = 1;
        if (option_height) *option_height = 1;
        return;
    }
    u32 option_h = settings_button_height();
    u32 popup_h = option_h * 3u;
    u32 popup_y = layout.row_y + layout.row_h - ui_px(2u);
    u32 bottom = (u32)win->y + win->titlebar_height + (u32)win->height
               - settings_footer_reserved() - ui_px(10u);
    if (popup_y + popup_h > bottom)
        popup_y = bottom > popup_h ? bottom - popup_h : (u32)win->y;

    if (x) *x = layout.control_x;
    if (y) *y = popup_y;
    if (width) *width = layout.control_w;
    if (height) *height = popup_h;
    if (option_height) *option_height = option_h;
}

static u32 settings_panel_width(void) {
    u32 desired = ui_px(760);
    u32 available = g_framebuffer.width > ui_px(24)
                  ? g_framebuffer.width-ui_px(24) : g_framebuffer.width;
    return desired > available ? available : desired;
}
static u32 settings_panel_height(void) {
    u32 content_height = settings_title_height() + ui_px(92) +
                         (3 * settings_row_height()) +
                         settings_footer_reserved();
    u32 navigation_height = settings_nav_top_offset() +
                            (u32)settings_catalog_count() * settings_nav_step() +
                            settings_nav_bottom_offset();
    return content_height > navigation_height
         ? content_height : navigation_height;
}

/* Recompute the Settings surface after a live font/UI-scale change. Its
 * content is measured in the new metrics, so the window must grow with it.
 * Center it in the usable workspace before resizing; this leaves enough room
 * on both sides for the bounded window-manager resize path. */
static bool settings_reflow_window(window_t* win) {
    if (!win || win->maximized || !g_framebuffer.initialized) return false;

    u32 screen_w = g_framebuffer.width;
    u32 screen_h = g_framebuffer.height;
    u32 work_bottom = screen_h > taskbar_height()
                    ? screen_h - taskbar_height() : screen_h;
    u32 margin = ui_px(24);
    u32 width = settings_panel_width();
    u32 total_height = settings_panel_height();
    u32 max_width = screen_w > margin ? screen_w - margin : screen_w;
    u32 max_height = work_bottom > margin ? work_bottom - margin : work_bottom;
    u32 title_h = settings_title_height();

    if (width > max_width) width = max_width;
    if (total_height > max_height) total_height = max_height;
    if (width <= ui_px(160) || total_height <= title_h + ui_px(64))
        return false;

    u32 client_height = total_height - title_h;
    s32 x = (s32)((screen_w - width) / 2u);
    s32 y = (s32)((work_bottom - total_height) / 2u);

    if (!wm_move_window(win, x, y)) return false;
    if (!wm_set_titlebar_height(win, title_h)) return false;
    if (!shell_resize_app_window(win, (int)width, (int)client_height))
        return false;

    serial_printf("SHELL_SETTINGS_REFLOW %ux%u at %d,%d\n",
                  width, client_height, win->x, win->y);
    serial_printf("SHELL_SETTINGS_LAYOUT x=%d y=%d width=%u total=%u "
                  "title=%u row=%u button=%u text=%u rail=%u nav_step=%u "
                  "footer=%u\n",
                  win->x, win->y, width, total_height, title_h,
                  settings_row_height_for(total_height),
                  settings_button_height(), text_height(),
                  settings_rail_width(), settings_nav_step(),
                  settings_footer_reserved());
    return true;
}

static void draw_settings_window(window_t* win) {
    if (!win || !settings_open) return;

    u32 cw = (u32)win->width;
    u32 ch = (u32)win->height + win->titlebar_height;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 title_h = settings_title_height();
    u32 row_h = settings_row_height_for(ch);
    u32 rail_w = settings_rail_width();
    if (rail_w > cw / 2u) rail_w = cw / 2u;

    const settings_page_t* page = settings_active_page();
    if (!page) return;

    u32 content_x = x + rail_w + ui_px(28);
    u32 content_w = cw > rail_w + ui_px(52)
                  ? cw - rail_w - ui_px(52) : 1u;
    u32 button_w = settings_button_width();
    u32 button_h = settings_button_height();
    u32 plus_x = x + cw - button_w - ui_px(24);
    u32 control_x = plus_x - button_w - ui_px(8);
    u32 first_row_y = y + title_h + ui_px(92);

    fb_fill_rect(x + ui_px(7), y + ui_px(8), cw, ch, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, cw, ch, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, cw, ch, CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(x, y, cw, title_h, CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x, y + title_h, cw, ui_px(1), CHARIS_UI_BORDER);
    fb_fill_rect(x + ui_px(1), y + title_h + ui_px(1), rail_w,
                 ch > title_h + ui_px(2) ? ch - title_h - ui_px(2) : 1u,
                 CHARIS_UI_PANEL_RAISED);
    settings_draw_navigation(x, y, rail_w, ch, title_h, page);

    u32 heading_y = y + title_h + ui_px(6);
    u32 heading_clip_w = control_x > content_x + ui_px(12)
                       ? control_x - content_x - ui_px(12) : 1u;
    fb_set_clip(content_x, heading_y, heading_clip_w, text_height());
    gfx_draw_string(content_x, heading_y, page->title,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_reset_clip();


    if (gfx_text_get_scale() == 1u) {
        settings_draw_wrapped_text(content_x,
            heading_y + text_height() + ui_px(6), content_w,
            page->summary, CHARIS_UI_MUTED, 2);
    }

    if (page->id == SETTINGS_PAGE_DISPLAY) {
        static const char* const font_names[] = {
            "Inter", "Monospace", "Bitmap serif"
        };
        static const char* const font_compact[] = { "Inter", "Mono", "Serif" };
        static const char* const text_sizes[] = { "100", "200", "300" };
        static const char* const ui_scales[] = { "80", "100", "120" };

        int style = config_get_int("font_style", 0);
        int font_scale = config_get_int("font_scale", 1);
        int global_scale = config_get_int("ui_scale", 100);
        if (style < 0 || style > 2) style = 0;
        if (font_scale < 1 || font_scale > 3) font_scale = 1;
        int scale_index = global_scale <= 80 ? 0 : global_scale >= 120 ? 2 : 1;

        for (u32 i = 0; i < 3u; i++) {
            settings_display_row_layout_t layout;
            if (!settings_display_row_layout(win, i, &layout)) continue;
            u32 row_draw_h = layout.row_h > ui_px(10u)
                           ? layout.row_h - ui_px(10u) : 1u;
            fb_fill_rect(layout.row_x, layout.row_y, layout.row_w,
                         row_draw_h, CHARIS_UI_SURFACE);
            fb_draw_rect(layout.row_x, layout.row_y, layout.row_w,
                         row_draw_h, CHARIS_UI_BORDER);

            const char* label = i == 0u ? "Font family" :
                                i == 1u ? "Text size (%)" : "UI scale (%)";
            if (font_scale >= 3) label = i == 0u ? "F" : i == 1u ? "T" : "UI";
            else if (font_scale > 1)
                label = i == 0u ? "Font" : i == 1u ? "Text" : "UI";

            fb_set_clip(layout.label_x, layout.row_y,
                        layout.label_w, row_draw_h);
            gfx_draw_string(layout.label_x, layout.label_y, label,
                            CHARIS_UI_TEXT, FB_TRANSPARENT);
            if (font_scale == 1) {
                const char* preview = i == 0u ? "Aa Bb Cc 0123" :
                                      i == 1u ? "The quick brown fox" : "";
                if (i < 2u) {
                    u32 preview_y = layout.label_y + text_height() + ui_px(4u);
                    fb_set_clip(layout.label_x, preview_y,
                                layout.label_w,
                                row_draw_h > preview_y - layout.row_y
                                  ? row_draw_h - (preview_y - layout.row_y) : 1u);
                    gfx_draw_string(layout.label_x, preview_y, preview,
                                    CHARIS_UI_MUTED, FB_TRANSPARENT);
                } else {
                    /* A miniature control sample follows the live UI scale. */
                    u32 preview_y = layout.label_y + text_height() + ui_px(5u);
                    u32 sample_h = ui_px(14u);
                    u32 sample_w = ui_px(34u);
                    fb_fill_rect(layout.label_x, preview_y, sample_w, sample_h,
                                 CHARIS_UI_PANEL_RAISED);
                    fb_draw_rect(layout.label_x, preview_y, sample_w, sample_h,
                                 CHARIS_UI_BORDER);
                    fb_fill_rect(layout.label_x + sample_w + ui_px(8u), preview_y,
                                 sample_w + ui_px(8u), sample_h,
                                 CHARIS_UI_ACCENT_SURFACE);
                    fb_draw_rect(layout.label_x + sample_w + ui_px(8u), preview_y,
                                 sample_w + ui_px(8u), sample_h,
                                 CHARIS_UI_BORDER_STRONG);
                }
            }
            fb_reset_clip();

            int mx = input_get_mouse_x(), my = input_get_mouse_y();
            bool over_control = mx >= (int)layout.control_x &&
                                mx < (int)(layout.control_x + layout.control_w) &&
                                my >= (int)layout.control_y &&
                                my < (int)(layout.control_y + layout.control_h);

            if (i == 0u) {
                fb_fill_rect(layout.control_x, layout.control_y,
                             layout.control_w, layout.control_h,
                             settings_font_menu_open ? CHARIS_UI_ACCENT_SURFACE :
                             over_control ? CHARIS_UI_SURFACE_RAISED :
                                            CHARIS_UI_PANEL_RAISED);
                fb_draw_rect(layout.control_x, layout.control_y,
                             layout.control_w, layout.control_h,
                             settings_font_menu_open ? CHARIS_UI_BORDER_STRONG :
                             over_control ? CHARIS_UI_BORDER_STRONG : CHARIS_UI_BORDER);
                u32 name_w = 0, name_h = 0;
                gfx_text_monospace_size(font_compact[style], &name_w, &name_h, 1u);
                gfx_draw_monospace_string_scaled(
                    layout.control_x + ui_px(10u),
                    layout.control_y + (layout.control_h > name_h
                                       ? (layout.control_h - name_h) / 2u : 0u),
                    font_compact[style], CHARIS_UI_ACCENT, FB_TRANSPARENT, 1u);
                gfx_draw_monospace_string_scaled(
                    layout.control_x + layout.control_w - ui_px(24u),
                    layout.control_y + (layout.control_h > name_h
                                       ? (layout.control_h - name_h) / 2u : 0u),
                    "v", CHARIS_UI_TEXT, FB_TRANSPARENT, 1u);
            } else {
                const char* const* choices = i == 1u ? text_sizes : ui_scales;
                int selected = i == 1u ? font_scale - 1 : scale_index;
                u32 segment_base = layout.control_w / 3u;
                for (u32 k = 0; k < 3u; k++) {
                    u32 sx = layout.control_x + k * segment_base;
                    u32 sw = k == 2u ? layout.control_w - segment_base * 2u
                                     : segment_base;
                    bool selected_segment = (int)k == selected;
                    bool hovered = mx >= (int)sx && mx < (int)(sx + sw) &&
                                   my >= (int)layout.control_y &&
                                   my < (int)(layout.control_y + layout.control_h);
                    fb_fill_rect(sx, layout.control_y, sw, layout.control_h,
                                 selected_segment ? CHARIS_UI_ACCENT_SURFACE :
                                 hovered ? CHARIS_UI_SURFACE_RAISED :
                                           CHARIS_UI_PANEL_RAISED);
                    fb_draw_rect(sx, layout.control_y, sw, layout.control_h,
                                 selected_segment ? CHARIS_UI_BORDER_STRONG :
                                 CHARIS_UI_BORDER);

                    u32 value_w = 0, value_h = 0;
                    gfx_text_monospace_size(choices[k], &value_w, &value_h, 1u);
                    gfx_draw_monospace_string_scaled(
                        sx + (sw > value_w ? (sw - value_w) / 2u : 0u),
                        layout.control_y + (layout.control_h > value_h
                                           ? (layout.control_h - value_h) / 2u : 0u),
                        choices[k],
                        selected_segment ? CHARIS_UI_TEXT : CHARIS_UI_MUTED,
                        FB_TRANSPARENT, 1u);
                }
            }
        }

        if (settings_font_menu_open) {
            u32 popup_x = 0, popup_y = 0, popup_w = 0, popup_h = 0, option_h = 0;
            settings_font_popup_bounds(win, &popup_x, &popup_y, &popup_w,
                                       &popup_h, &option_h);
            fb_fill_rect(popup_x + ui_px(3u), popup_y + ui_px(3u),
                         popup_w, popup_h, CHARIS_UI_SHADOW);
            fb_fill_rect(popup_x, popup_y, popup_w, popup_h,
                         CHARIS_UI_PANEL_RAISED);
            fb_draw_rect(popup_x, popup_y, popup_w, popup_h,
                         CHARIS_UI_BORDER_STRONG);
            int mx = input_get_mouse_x(), my = input_get_mouse_y();
            for (u32 i = 0; i < 3u; i++) {
                u32 option_y = popup_y + i * option_h;
                bool selected = (int)i == style;
                bool hovered = mx >= (int)popup_x && mx < (int)(popup_x + popup_w) &&
                               my >= (int)option_y && my < (int)(option_y + option_h);
                fb_fill_rect(popup_x + ui_px(2u), option_y + ui_px(1u),
                             popup_w - ui_px(4u), option_h - ui_px(2u),
                             selected ? CHARIS_UI_ACCENT_SURFACE :
                             hovered ? CHARIS_UI_SURFACE_RAISED :
                                       CHARIS_UI_PANEL_RAISED);
                u32 option_w = 0, option_text_h = 0;
                gfx_text_monospace_size(font_names[i], &option_w,
                                        &option_text_h, 1u);
                fb_draw_rect(popup_x + ui_px(2u), option_y + ui_px(1u),
                             popup_w - ui_px(4u), option_h - ui_px(2u),
                             CHARIS_UI_BORDER);
                gfx_draw_monospace_string_scaled(
                    popup_x + ui_px(12u),
                    option_y + (option_h > option_text_h
                              ? (option_h - option_text_h) / 2u : 0u),
                    font_names[i],
                    selected ? CHARIS_UI_TEXT : CHARIS_UI_MUTED,
                    FB_TRANSPARENT, 1u);
            }
        }
    } else if (page->id == SETTINGS_PAGE_APPEARANCE) {
        int selected = config_get_int("wallpaper", (int)WALLPAPER_CHARIS);
        if (selected < 0 || selected >= (int)wallpaper_count())
            selected = (int)WALLPAPER_CHARIS;

        u32 row_y = first_row_y;
        u32 row_draw_h = row_h > ui_px(10) ? row_h - ui_px(10) : 1u;
        u32 row_w = cw > rail_w + ui_px(30)
                  ? cw - rail_w - ui_px(30) : 1u;
        fb_fill_rect(content_x - ui_px(10), row_y, row_w,
                     row_draw_h, CHARIS_UI_SURFACE);
        fb_draw_rect(content_x - ui_px(10), row_y, row_w,
                     row_draw_h, CHARIS_UI_BORDER);

        u32 label_x = content_x + ui_px(8);
        u32 label_y = row_y + ui_px(16);
        fb_set_clip(label_x, row_y + ui_px(8),
                    control_x > label_x + ui_px(12)
                      ? control_x - label_x - ui_px(12) : 1u,
                    row_draw_h > ui_px(16) ? row_draw_h - ui_px(16) : 1u);
        gfx_draw_string(label_x, label_y,
                        "Desktop background", CHARIS_UI_TEXT, FB_TRANSPARENT);
        gfx_draw_string(label_x, label_y + text_height() + ui_px(5),
                        wallpaper_description((u32)selected),
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
        fb_reset_clip();

        u32 by = row_y + (row_h - button_h) / 2u;
        fb_fill_rect(control_x, by, button_w, button_h,
                     CHARIS_UI_PANEL_RAISED);
        fb_draw_rect(control_x, by, button_w, button_h, CHARIS_UI_BORDER);
        fb_fill_rect(plus_x, by, button_w, button_h,
                     CHARIS_UI_ACCENT_SURFACE);
        fb_draw_rect(plus_x, by, button_w, button_h,
                     CHARIS_UI_BORDER_STRONG);
        u32 minus_w = 0, glyph_h = 0, plus_w = 0;
        gfx_text_size("-", &minus_w, &glyph_h);
        gfx_text_size("+", &plus_w, NULL);
        u32 glyph_y = by + (button_h > glyph_h
                      ? (button_h - glyph_h) / 2u : 0u);
        gfx_draw_string(control_x + (button_w > minus_w
                        ? (button_w - minus_w) / 2u : 0u),
                        glyph_y, "-", CHARIS_UI_TEXT, FB_TRANSPARENT);
        gfx_draw_string(plus_x + (button_w > plus_w
                        ? (button_w - plus_w) / 2u : 0u),
                        glyph_y, "+", CHARIS_UI_TEXT, FB_TRANSPARENT);

        const char* name = wallpaper_name((u32)selected);
        u32 name_w = 0, name_h = 0;
        gfx_text_size(name, &name_w, &name_h);
        u32 value_x = control_x > name_w + ui_px(10)
                    ? control_x - name_w - ui_px(10) : content_x;
        fb_set_clip(value_x, by,
                    control_x > value_x ? control_x - value_x : 1u,
                    button_h);
        gfx_draw_string(value_x, by + (button_h > name_h
                        ? (button_h - name_h) / 2u : 0u),
                        name, CHARIS_UI_ACCENT, FB_TRANSPARENT);
        fb_reset_clip();

        u32 note_y = first_row_y + row_h + ui_px(8);
        settings_draw_wrapped_text(content_x, note_y, content_w,
            page->state_detail, CHARIS_UI_MUTED, 3);
    } else {
        u32 card_y = first_row_y;
        u32 footer_top = y + ch - settings_footer_reserved() - ui_px(12);
        u32 card_h = footer_top > card_y + ui_px(16)
                   ? footer_top - card_y : ui_px(80);
        fb_fill_rect(content_x - ui_px(10), card_y, content_w + ui_px(10),
                     card_h, CHARIS_UI_SURFACE);
        fb_draw_rect(content_x - ui_px(10), card_y, content_w + ui_px(10),
                     card_h, CHARIS_UI_BORDER);

        u32 state_y = card_y + ui_px(18);
        u32 state_color = page->state == SETTINGS_PAGE_READY
                        ? CHARIS_UI_SUCCESS : CHARIS_UI_WARNING;
        gfx_draw_string(content_x + ui_px(8), state_y,
                        "Capability status", CHARIS_UI_MUTED, FB_TRANSPARENT);
        gfx_draw_string(content_x + ui_px(8),
                        state_y + text_height() + ui_px(8),
                        settings_page_state_label(page->state),
                        state_color, FB_TRANSPARENT);
        u32 detail_x = content_x + ui_px(8);
        u32 detail_y = state_y + text_height() * 2u + ui_px(20);
        u32 detail_bottom = card_y + card_h - ui_px(10);
        u32 detail_available = detail_bottom > detail_y
                             ? detail_bottom - detail_y : 0u;
        u32 detail_line_step = text_height() + ui_px(4);
        if (detail_line_step && detail_available >= detail_line_step) {
            u32 detail_lines = detail_available / detail_line_step;
            if (detail_lines > 5u) detail_lines = 5u;
            settings_draw_wrapped_text(detail_x, detail_y,
                content_w > ui_px(20) ? content_w - ui_px(20) : 1u,
                page->state_detail, CHARIS_UI_TEXT, detail_lines);
        }
    }

    u32 footer_y = y + ch - ui_px(12) - text_height();
    u32 footer_w = cw > rail_w + ui_px(36)
                 ? cw - rail_w - ui_px(36) : 1u;
    fb_fill_rect(x + rail_w + ui_px(18), footer_y - ui_px(12),
                 footer_w, ui_px(1), CHARIS_UI_BORDER);
    fb_set_clip(content_x, footer_y, footer_w, text_height());
    gfx_draw_string(content_x, footer_y,
        config_is_persistent()
            ? (gfx_text_get_scale() >= 2u ? "Saved" : "All changes saved")
            : (gfx_text_get_scale() >= 3u ? "Session only" :
               (gfx_text_get_scale() >= 2u ? "Session only" :
                "Session only  |  No writable configuration volume")),
        config_is_persistent() ? CHARIS_UI_SUCCESS : CHARIS_UI_WARNING,
        FB_TRANSPARENT);
    fb_reset_clip();
}

static bool settings_handle_window_click(window_t* win, int mx, int my) {
    if (!win) return false;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 cw = (u32)win->width, ch = (u32)win->height + win->titlebar_height;
    if (mx < (int)x || mx >= (int)(x + cw) ||
        my < (int)y || my >= (int)(y + ch)) return false;

    const settings_page_t* active = settings_active_page();
    if (!active) return true;

    if (settings_font_menu_open && active->id == SETTINGS_PAGE_DISPLAY) {
        u32 popup_x = 0, popup_y = 0, popup_w = 0, popup_h = 0, option_h = 0;
        settings_font_popup_bounds(win, &popup_x, &popup_y, &popup_w,
                                   &popup_h, &option_h);
        if (mx >= (int)popup_x && mx < (int)(popup_x + popup_w) &&
            my >= (int)popup_y && my < (int)(popup_y + popup_h)) {
            u32 choice = (u32)(my - (int)popup_y) / option_h;
            if (choice < 3u) {
                config_set_int("font_style", (int)choice);
                gfx_text_apply_preferences();
                (void)settings_reflow_window(win);
                (void)config_save(CONFIG_FILE_PATH);
            }
            settings_font_menu_open = false;
            return true;
        }
        settings_font_menu_open = false;
    }

    u32 rail_w = settings_rail_width();
    if (rail_w > cw / 2u) rail_w = cw / 2u;
    if (mx < (int)(x + rail_w)) {
        int page_index = settings_nav_hit_page(win, mx, my);
        if (page_index >= 0) {
            const settings_page_t* page =
                settings_catalog_at((usize)page_index);
            if (page) settings_select_page(page->id);
        }
        return true;
    }

    u32 first_row_y = y + settings_title_height() + ui_px(92u);
    u32 row_h = settings_row_height_for(ch);
    if (my < (int)first_row_y) return true;

    if (active->id == SETTINGS_PAGE_DISPLAY) {
        u32 row = (u32)(my - (int)first_row_y) / row_h;
        if (row >= 3u) return true;
        settings_display_row_layout_t layout;
        if (!settings_display_row_layout(win, row, &layout)) return true;
        if (mx < (int)layout.control_x ||
            mx >= (int)(layout.control_x + layout.control_w) ||
            my < (int)layout.control_y ||
            my >= (int)(layout.control_y + layout.control_h))
            return true;

        if (row == 0u) {
            settings_font_menu_open = !settings_font_menu_open;
            return true;
        }

        u32 segment_width = layout.control_w / 3u;
        if (!segment_width) return true;
        u32 choice = (u32)(mx - (int)layout.control_x) / segment_width;
        if (choice > 2u) choice = 2u;
        if (row == 1u) {
            config_set_int("font_scale", (int)choice + 1);
        } else {
            static const int values[] = { 80, 100, 120 };
            config_set_int("ui_scale", values[choice]);
        }

        gfx_text_apply_preferences();
        (void)settings_reflow_window(win);
        (void)config_save(CONFIG_FILE_PATH);
        return true;
    }

    if (active->id == SETTINGS_PAGE_APPEARANCE) {
        u32 button_w = settings_button_width();
        u32 plus_x = x + cw - button_w - ui_px(24u);
        u32 control_x = plus_x - button_w - ui_px(8u);
        u32 first_y = y + settings_title_height() + ui_px(92u);
        u32 row_height = settings_row_height_for(ch);
        int direction = mx >= (int)plus_x &&
                        mx < (int)(plus_x + button_w) ? 1 :
                        mx >= (int)control_x &&
                        mx < (int)(control_x + button_w) ? -1 : 0;
        if (direction && my >= (int)first_y &&
            my < (int)(first_y + row_height))
            settings_adjust_wallpaper(direction);
        return true;
    }

    return true;
}

/* Software cursor. The PS/2 mouse has no hardware cursor once the display is
 * in graphics mode, so the pointer is drawn into the framebuffer. */
void ui_draw_cursor(void) {
    cursor_draw();
}

static void redraw_all(void) {
    /* Any old save-under belongs to the previous desktop frame. Restore it
     * before repainting so cursor pixels never become part of the wallpaper. */
    cursor_erase();
    wallpaper_draw((u32)config_get_int("wallpaper", 0));
    desktop_surface_render();
    draw_taskbar();
    wm_render_surfaces();
    draw_menu();
    draw_desktop_context_menu();
    ui_draw_cursor();
}

/* ── Hit testing ──────────────────────────────────────────────────── */

static bool menu_contains(int mx, int my) {
    if (!ui.menu_open) return false;
    int results[MENU_ITEM_COUNT];
    u32 count = menu_build_results(results, MENU_ITEM_COUNT);
    u32 menu_h = menu_height_for_count(count);
    u32 screen_h = g_framebuffer.height;
    u32 bar_h = taskbar_height();
    u32 top = screen_h > bar_h + menu_h ? screen_h - bar_h - menu_h : 0u;
    return mx >= 0 && mx < (int)MENU_W &&
           my >= (int)top && my < (int)(top + menu_h);
}

static int hit_menu_item(int mx, int my) {
    if (!ui.menu_open) return -1;
    int results[MENU_ITEM_COUNT];
    u32 count = menu_build_results(results, MENU_ITEM_COUNT);
    u32 rows = count ? count : 1u;
    u32 menu_h = menu_height_for_count(count);
    u32 h = g_framebuffer.height;
    u32 bar_h = taskbar_height();
    u32 top = h > bar_h + menu_h ? h - bar_h - menu_h : 0u;
    u32 list_top = top + ui_px(50u);

    if (mx < 0 || mx >= (int)MENU_W ||
        my < (int)list_top) return -1;
    u32 row = (u32)(my - (int)list_top) / MENU_ITEM_H;
    if (row >= count || row >= rows) return -1;
    return results[row];
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
        case SHELL_APP_CALCULATOR: calculator_ui_close(); break;
        case SHELL_APP_BROWSER: browser_close(); break;
        case SHELL_APP_SETTINGS:
            settings_open = false;
            settings_font_menu_open = false;
            break;
        case SHELL_APP_ABOUT:
            notice_open = false;
            notice_item = -1;
            break;
        default: return;
    }
    shell_sync_app_windows();
    serial_puts("SHELL_WINDOW_CLOSED");
}

static bool shell_handle_window_chrome(window_t* win, int mx, int my) {
    int id = shell_app_id_for_window(win);
    if (id < 0 || !win) return false;
    u32 x = (u32)win->x, y = (u32)win->y;
    u32 width = (u32)win->width, height = win->titlebar_height;
    if (mx < (int)x || mx >= (int)(x + width) ||
        my < (int)y || my >= (int)(y + height)) return false;

    u32 button_w = ui_px(36u), button_h = ui_px(28u);
    u32 gap = ui_px(2u), margin = ui_px(6u);
    if (width < button_w * 3u + gap * 2u + margin * 2u) return false;
    if (height < button_h) button_h = height;
    u32 close_x = x + width - margin - button_w;
    u32 maximize_x = close_x - gap - button_w;
    u32 minimize_x = maximize_x - gap - button_w;
    u32 button_y = y + (height - button_h) / 2u;
    if (my < (int)button_y || my >= (int)(button_y + button_h)) return false;

    if (mx >= (int)close_x && mx < (int)(close_x + button_w)) {
        shell_close_focused_app();
        return true;
    }
    if (mx >= (int)maximize_x && mx < (int)(maximize_x + button_w)) {
        if (win->maximized) {
            if (wm_restore_window_geometry(win))
                serial_puts("SHELL_WINDOW_RESTORED");
        } else if (wm_maximize_window(
                       win, g_framebuffer.height - taskbar_height())) {
            serial_puts("SHELL_WINDOW_MAXIMIZED");
        }
        return true;
    }
    if (mx >= (int)minimize_x && mx < (int)(minimize_x + button_w)) {
        (void)wm_minimize_window(win);
        serial_printf("SHELL_WINDOW_MINIMIZED %s", shell_app_title((shell_app_id_t)id));
        return true;
    }
    return false;
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
        if (shell_keyboard_resize_window) {
            if (!pressed) return;
            if (extended) {
                int width = shell_keyboard_resize_window->width;
                int height = shell_keyboard_resize_window->height;
                int step = (int)ui_px(20);
                if (step < 1) step = 1;
                if (base == 0x4Du) width += step;       /* Right */
                else if (base == 0x4Bu) width -= step;  /* Left */
                else if (base == 0x50u) height += step; /* Down */
                else if (base == 0x48u) height -= step; /* Up */
                else return;
                (void)shell_resize_app_window(shell_keyboard_resize_window,
                                               width, height);
                ui.needs_redraw = true;
                return;
            }
            if (base == 0x1Cu) {
                shell_keyboard_resize_end(true);
                ui.needs_redraw = true;
                return;
            }
            if (base == KEY_ESCAPE) {
                shell_keyboard_resize_end(false);
                ui.needs_redraw = true;
                return;
            }
            return;
        }

        if (pressed && !extended && base == 0x42u && alt_held) {
            shell_keyboard_resize_start();
            ui.needs_redraw = true;
            return;
        }

        /* F8 launches Calculator; Alt+F8 remains the keyboard resize mode. */
        if (pressed && !extended && base == 0x42u && !alt_held) {
            ui.menu_open = false;
            notice_open = false;
            notice_item = -1;
            shell_open_app(SHELL_APP_CALCULATOR);
            ui.needs_redraw = true;
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
            if (pressed) shell_menu_toggle();
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
        if (pressed && !extended && base == 0x43u) {
            ui.menu_open = false;
            notice_open = false;
            shell_open_app(SHELL_APP_BROWSER);
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
            if (desktop_context_menu_open) {
                desktop_context_menu_open = false;
                desktop_context_hover = -1;
                ui.needs_redraw = true;
                return;
            }
            if (ui.menu_open) {
                ui.menu_open = false;
                ui.hover_item = -1;
                ui.needs_redraw = true;
                return;
            }
        }

        if (ui.menu_open) return;
        wm_dispatch_event(e->type, (void*)e);
        ui.needs_redraw = true;
        return;
    }

    case INPUT_MOUSE_MOVE: {
        bool content_changed = false;
        int context_hover = desktop_context_menu_hit(e->mouse.x, e->mouse.y);
        if (context_hover != desktop_context_hover) {
            desktop_context_hover = context_hover;
            content_changed = true;
        }
        if (shell_resize_window) {
            int requested_w = shell_resize_start_width +
                              e->mouse.x - shell_resize_origin_x;
            int requested_h = shell_resize_start_height +
                              e->mouse.y - shell_resize_origin_y;
            (void)shell_resize_app_window(shell_resize_window,
                                          requested_w, requested_h);
            content_changed = true;
        } else if (shell_drag_window) {
            content_changed = wm_move_window(shell_drag_window,
                                             e->mouse.x - shell_drag_offset_x,
                                             e->mouse.y - shell_drag_offset_y);
        }
        int hover = hit_menu_item(e->mouse.x, e->mouse.y);
        int shortcut_hover = desktop_surface_hit_test(e->mouse.x, e->mouse.y);
        if (shortcut_hover != desktop_hover_shortcut) {
            desktop_hover_shortcut = shortcut_hover;
            content_changed = true;
        }
        int settings_hover = settings_nav_hit_page(
            shell_app_windows[SHELL_APP_SETTINGS], e->mouse.x, e->mouse.y);
        if (settings_hover != settings_hover_page_index) {
            settings_hover_page_index = settings_hover;
            content_changed = true;
        }
        if (hover != ui.hover_item) {
            ui.hover_item = hover;
            content_changed = true;
        }
        /* Ordinary pointer movement is handled by the save-under cursor.
         * Only repaint the desktop when a real surface or hover state changed. */
        if (content_changed) ui.needs_redraw = true;
        return;
    }

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
        if (desktop_context_menu_open) {
            if (desktop_context_menu_contains(mx, my)) {
                if (e->mouse.button == 1) {
                    int context_hit = desktop_context_menu_hit(mx, my);
                    if (context_hit >= 0) desktop_context_menu_select(context_hit);
                }
                return;
            }
            desktop_context_menu_open = false;
            desktop_context_hover = -1;
            ui.needs_redraw = true;
        }
        if (ui.menu_open) {
            if (menu_contains(mx, my)) {
                int menu_hit = hit_menu_item(mx, my);
                if (menu_hit >= 0) shell_menu_select_index(menu_hit);
                else ui.needs_redraw = true;
                return;
            }

            /* Clicking the Start button while the menu is open dismisses it;
             * a second click is required to reopen it, as with a normal shell. */
            ui.menu_open = false;
            ui.hover_item = -1;
            ui.needs_redraw = true;
            u32 menu_bar_h = taskbar_height();
            u32 menu_bar_top = g_framebuffer.height > menu_bar_h
                             ? g_framebuffer.height - menu_bar_h : 0u;
            if (my >= (int)menu_bar_top && mx >= 0 &&
                mx < (int)START_BTN_W) return;
        }
        window_t* hit = wm_get_window_at(mx, my);
        if (e->mouse.button == 2 && !hit) {
            u32 right_click_bar = g_framebuffer.height > taskbar_height()
                                ? g_framebuffer.height - taskbar_height()
                                : g_framebuffer.height;
            if (my < (int)right_click_bar) {
                desktop_context_menu_open_at(mx, my);
                return;
            }
        }
        if (hit) {
            wm_focus_window(hit);
            if (e->mouse.button == 1 && shell_app_id_for_window(hit) >= 0 &&
                shell_resize_grip_hit(hit, mx, my)) {
                serial_printf("SHELL_RESIZE_GRIP_ARMED %d,%d\n", mx, my);
                shell_resize_window = hit;
                shell_drag_window = NULL;
                shell_resize_origin_x = mx;
                shell_resize_origin_y = my;
                shell_resize_start_width = hit->width;
                shell_resize_start_height = hit->height;
                ui.needs_redraw = true;
                return;
            }
            if (e->mouse.button == 1 && shell_app_id_for_window(hit) >= 0 &&
                my >= hit->y && my < hit->y + (int)hit->titlebar_height) {
                if (shell_handle_window_chrome(hit, mx, my)) {
                    ui.needs_redraw = true;
                    return;
                }
                if (hit->maximized) {
                    (void)wm_restore_window_geometry(hit);
                    shell_drag_offset_x = hit->width / 2;
                    shell_drag_offset_y = (int)hit->titlebar_height / 2;
                } else {
                    shell_drag_offset_x = mx - hit->x;
                    shell_drag_offset_y = my - hit->y;
                }
                shell_drag_window = hit;
                ui.needs_redraw = true;
                return;
            }
            shell_pointer_handled = false;
            wm_dispatch_event(INPUT_MOUSE_DOWN, (void*)e);
            shell_drag_window = NULL;
            ui.needs_redraw = true;
            return;
        }
        shell_drag_window = NULL;

        u32 bar_h = taskbar_height();
        u32 bar_top = g_framebuffer.height > bar_h ? g_framebuffer.height - bar_h : 0;
        bool on_taskbar = my >= (int)bar_top;

        if (on_taskbar) {
            int running_app = taskbar_app_hit_test(mx, my, bar_top);
            if (running_app >= 0) {
                ui.menu_open = false;
                shell_toggle_app((shell_app_id_t)running_app);
                ui.needs_redraw = true;
                return;
            }
        }

        if (on_taskbar && mx < (int)START_BTN_W) {
            desktop_context_menu_open = false;
            desktop_context_hover = -1;
            shell_menu_toggle();
            return;
        }

        int shortcut = desktop_surface_hit_test(mx, my);
        if (shortcut >= 0 && shortcut < 4) {
            desktop_context_menu_open = false;
            desktop_context_hover = -1;
            static const shell_app_id_t shortcut_apps[4] = {
                SHELL_APP_FILES, SHELL_APP_TERMINAL,
                SHELL_APP_SETTINGS, SHELL_APP_BROWSER
            };
            ui.menu_open = false;
            shell_open_app(shortcut_apps[shortcut]);
            ui.needs_redraw = true;
            return;
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
        while (input_pop_event(&e)) {
            /* Event handlers decide whether an event changes a visible surface.
             * In particular, pointer motion must not schedule a full desktop
             * repaint unless a window or hover state actually changed. */
            handle_event(&e);
        }
        char key;
        bool terminal_has_focus =
            shell_app_windows[SHELL_APP_TERMINAL] &&
            wm_get_focused() == shell_app_windows[SHELL_APP_TERMINAL] &&
            !shell_app_windows[SHELL_APP_TERMINAL]->minimized;
        while (keyboard_get_key(&key)) {
            if (ui.menu_open) {
                shell_menu_handle_character(key);
            } else if (terminal_has_focus) {
                terminal_handle_char(key);
                ui.needs_redraw = true;
            }
        }
        shell_sync_app_windows();

        /* Cursor blink. */
        u64 now = timer_get_ms();
        if (now >= ui.next_blink_ms) {
            ui.cursor_blink = !ui.cursor_blink;
            ui.next_blink_ms = now + CURSOR_BLINK;
        }
        if (now >= next_clock_refresh_ms) {
            next_clock_refresh_ms = now + 60000u;
            /* Restore the pointer before a partial taskbar paint. The cursor
             * will be composited again at the end of this frame, with a fresh
             * save-under snapshot for pixels the clock may have changed. */
            if (!ui.needs_redraw) {
                u32 bar_h = taskbar_height();
                u32 bar_y = g_framebuffer.height > bar_h
                          ? g_framebuffer.height - bar_h : 0;
                /* A clock repaint must not erase a pointer elsewhere on the
                 * desktop. Restore save-under only when this paint intersects it. */
                if (cursor_intersects_rect(0, bar_y, g_framebuffer.width, bar_h))
                    cursor_erase();
                draw_taskbar();
            }
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
        /* Cursor compositing is independent from desktop painting. It restores
         * the old save-under and paints only the new 10x16 pointer footprint. */
        ui_draw_cursor();

        /* The scheduler is cooperative: yield after each frame so a busy
         * desktop cannot monopolize the CPU while waiting for input. */
        scheduler_yield();
        asm volatile("hlt");
    }
}
