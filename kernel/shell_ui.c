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
    { "Browser" },
    { "Terminal" },
    { "Files" },
    { "Settings" },
    { "About CharisOS" },
};

static bool notice_open = false;
static int notice_item = -1;
static bool settings_open = false;

static const char* const NOTICE_LINE_1[] = {
    "CharisOS is a from-scratch x86_64 kernel.",
};
static const char* const NOTICE_LINE_2[] = {
    "The desktop and built-in apps are still evolving.",
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
        draw_app_icon(i, x + ui_px(12), iy + ui_px(7));
        gfx_draw_string(x + ui_px(44), iy + (MENU_ITEM_H > text_height() ? (MENU_ITEM_H - text_height()) / 2 : 0),
                        MENU_ITEMS[i].label, T_FG, FB_TRANSPARENT);
    }
}

/* A launcher item must not silently send output to an invisible VGA console.
 * Until each application has a real framebuffer UI, explain its current state
 * in a consistent, dismissible panel instead of pretending it launched. */
static void draw_notice(void) {
    if (!notice_open || notice_item < 0 || notice_item >= (int)MENU_ITEM_COUNT) return;

    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = ui_px(440), ch = ui_px(152);
    u32 x = sw > cw ? (sw - cw) / 2 : 0;
    u32 y = sh > ch ? (sh - ch) / 2 : 0;

    fb_fill_rect(x + ui_px(4), y + ui_px(4), cw, ch, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, cw, ch, T_MENU);
    fb_draw_rect(x, y, cw, ch, T_BORDER);
    fb_fill_rect(x, y, cw, ui_px(28), T_START_BTN);
    fb_fill_rect(x, y, cw, 1, CHARIS_UI_BORDER_STRONG);
    gfx_draw_string(x + ui_px(16), y + ui_px(7), MENU_ITEMS[notice_item].label, T_FG, FB_TRANSPARENT);

    gfx_draw_string(x + ui_px(20), y + ui_px(48), NOTICE_LINE_1[notice_item], T_FG, FB_TRANSPARENT);
    gfx_draw_string(x + ui_px(20), y + ui_px(72), NOTICE_LINE_2[notice_item], T_FG_DIM, FB_TRANSPARENT);

    u32 bw = ui_px(88), bh = ui_px(28);
    u32 bx = x + cw - bw - ui_px(16), by = y + ch - bh - ui_px(12);
    fb_fill_rect(bx, by, bw, bh, CHARIS_UI_ACCENT_SURFACE);
    fb_draw_rect(bx, by, bw, bh, CHARIS_UI_BORDER_STRONG);
    gfx_draw_string(bx + ui_px(24), by + ui_px(6), "Close", T_FG, FB_TRANSPARENT);
}

/* Native Settings application: navigation rail plus a focused settings page. */
static u32 settings_title_height(void) { return ui_px(52); }
static u32 settings_row_height(void) { return ui_px(100); }
static u32 settings_button_width(void) { return ui_px(32); }
static u32 settings_panel_width(void) { return ui_px(760); }
static u32 settings_panel_height(void) {
    return settings_title_height() + ui_px(90) +
           (3 * settings_row_height()) + ui_px(58);
}

static void draw_settings(void) {
    if (!settings_open) return;
    static const char* const styles[] = { "Proportional", "Monospace", "Bitmap serif" };
    static const char* const scales[] = { "100%", "200%", "300%" };
    static const char* const ui_scales[] = { "80%", "100%", "120%" };
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = settings_panel_width(), ch = settings_panel_height();
    u32 x = sw > cw ? (sw-cw)/2u : 0, y = sh > ch ? (sh-ch)/2u : 0;
    u32 title_h = settings_title_height(), row_h = settings_row_height();
    u32 rail_w = ui_px(164), content_x = x+rail_w+ui_px(28);
    u32 control_x = x+ui_px(548), button_w = settings_button_width();
    u32 plus_x = x+cw-button_w-ui_px(24), button_h = ui_px(34);

    fb_fill_rect(x+ui_px(7), y+ui_px(8), cw, ch, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, cw, ch, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, cw, ch, CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(x, y, cw, title_h, CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x, y+title_h, cw, ui_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(x+ui_px(22), y+(title_h-text_height())/2u,
                    "Settings", CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x+ui_px(104), y+(title_h-text_height())/2u,
                    "SYSTEM PREFERENCES", CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_fill_rect(x+cw-ui_px(42), y+ui_px(12), ui_px(26), ui_px(26),
                 CHARIS_UI_SURFACE);
    fb_draw_rect(x+cw-ui_px(42), y+ui_px(12), ui_px(26), ui_px(26),
                 CHARIS_UI_BORDER);
    gfx_draw_string(x+cw-ui_px(34), y+ui_px(18), "x",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);

    /* Navigation rail. */
    fb_fill_rect(x+ui_px(1), y+title_h+ui_px(1), rail_w,
                 ch-title_h-ui_px(2), CHARIS_UI_PANEL_RAISED);
    gfx_draw_string(x+ui_px(18), y+title_h+ui_px(24),
                    "PREFERENCES", CHARIS_UI_MUTED, FB_TRANSPARENT);
    const char* nav[] = { "Display", "Personalization", "System" };
    for (u32 i=0; i<3; i++) {
        u32 ny = y+title_h+ui_px(54)+i*ui_px(42);
        if (i==0) {
            fb_fill_rect(x+ui_px(8), ny-ui_px(8), rail_w-ui_px(16),
                         ui_px(34), CHARIS_UI_ACCENT_SURFACE);
            fb_fill_rect(x+ui_px(8), ny-ui_px(8), ui_px(3),
                         ui_px(34), CHARIS_UI_ACCENT);
        }
        gfx_draw_string(x+ui_px(22), ny, nav[i],
                        i==0 ? CHARIS_UI_TEXT : CHARIS_UI_MUTED,
                        FB_TRANSPARENT);
    }

    /* Display page. */
    gfx_draw_string(content_x, y+title_h+ui_px(22),
                    "Display", CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(content_x, y+title_h+ui_px(48),
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
        "Typeface used by native applications",
        "Adjust text legibility across the desktop",
        "Resize windows, controls and spacing"
    };
    for (u32 i=0; i<3; i++) {
        u32 ry = y+title_h+ui_px(82)+i*row_h;
        fb_fill_rect(content_x-ui_px(10), ry, cw-rail_w-ui_px(30),
                     row_h-ui_px(10), CHARIS_UI_SURFACE);
        fb_draw_rect(content_x-ui_px(10), ry, cw-rail_w-ui_px(30),
                     row_h-ui_px(10), CHARIS_UI_BORDER);
        gfx_draw_string(content_x+ui_px(8), ry+ui_px(22),
                        labels[i], CHARIS_UI_TEXT, FB_TRANSPARENT);
        gfx_draw_string(content_x+ui_px(8), ry+ui_px(52),
                        hints[i], CHARIS_UI_MUTED, FB_TRANSPARENT);
        u32 by = ry+(row_h-button_h)/2u-ui_px(4);
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
        gfx_draw_string(control_x+button_w+ui_px(12),
                        by+(button_h-text_height())/2u,
                        values[i], CHARIS_UI_ACCENT, FB_TRANSPARENT);
    }

    u32 footer_y = y+ch-ui_px(34);
    fb_fill_rect(x+rail_w+ui_px(18), footer_y-ui_px(12),
                 cw-rail_w-ui_px(36), ui_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(content_x, footer_y,
                    config_is_persistent() ? "All changes saved"
                                           : "Session only  |  No writable configuration volume",
                    config_is_persistent() ? CHARIS_UI_SUCCESS : CHARIS_UI_WARNING,
                    FB_TRANSPARENT);
}

static bool settings_handle_click(int mx, int my) {
    if (!settings_open) return false;
    u32 sw=g_framebuffer.width, sh=g_framebuffer.height;
    u32 cw=settings_panel_width(), ch=settings_panel_height();
    u32 x=sw>cw ? (sw-cw)/2u : 0, y=sh>ch ? (sh-ch)/2u : 0;
    if (mx<(int)x || mx>=(int)(x+cw) || my<(int)y || my>=(int)(y+ch)) {
        settings_open=false; return true;
    }
    if (mx >= (int)(x+cw-ui_px(48)) && my < (int)(y+settings_title_height())) {
        settings_open=false; return true;
    }
    u32 rail_w=ui_px(164), row_h=settings_row_height();
    u32 control_x=x+ui_px(548), button_w=settings_button_width();
    u32 plus_x=x+cw-button_w-ui_px(24);
    u32 first_y=y+settings_title_height()+ui_px(82);
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
    browser_render();
    draw_taskbar();
    draw_menu();
    draw_notice();
    draw_settings();
    terminal_render();
    files_render();
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

static void handle_event(const input_event_t* e) {
    switch (e->type) {
    case INPUT_KEY_DOWN:
    case INPUT_KEY_UP: {
        u8 code = e->key.scancode;
        bool extended = (code & 0x80) != 0;
        u8 base = code & 0x7F;

        /* The terminal is modal. ASCII characters are consumed from the
         * keyboard ring after event dispatch; Escape closes the window. */
        if (terminal_is_open()) {
            if (e->type == INPUT_KEY_DOWN && !extended && base == KEY_ESCAPE) {
                terminal_close();
                ui.needs_redraw = true;
            }
            return;
        }
        if (files_is_open()) {
            if (e->type == INPUT_KEY_DOWN)
                files_handle_key((u8)(base | (extended ? 0x80u : 0u)), true);
            ui.needs_redraw = true;
            return;
        }

        /* F10 opens Files without requiring a working pointer device. */
        if (e->type == INPUT_KEY_DOWN && !extended && base == 0x44) {
            ui.menu_open = false;
            ui.hover_item = -1;
            notice_open = false;
            settings_open = false;
            files_open();
            ui.needs_redraw = true;
            return;
        }

        /* F12 is a reliable keyboard-only Terminal shortcut. It also
         * gives headless systems and accessibility setups a way to open the
         * command surface without depending on pointer hardware. */
        if (e->type == INPUT_KEY_DOWN && !extended && base == 0x58) {
            ui.menu_open = false;
            ui.hover_item = -1;
            notice_open = false;
            settings_open = false;
            terminal_open();
            ui.needs_redraw = true;
            return;
        }

        /* Super is 0x5B/0x5C, both extended. */
        if (extended && (base == 0x5B || base == 0x5C)) {
            super_held = (e->type == INPUT_KEY_DOWN);
            if (super_held) {
                ui.menu_open = !ui.menu_open;
                ui.hover_item = -1;
                ui.needs_redraw = true;
            }
            return;
        }

        if (browser_is_open()) {
            browser_handle_key(base, e->type == INPUT_KEY_DOWN);
            ui.needs_redraw = true;
            return;
        }

        /* Escape dismisses the topmost transient surface. */
        if (e->type == INPUT_KEY_DOWN && base == KEY_ESCAPE) {
            if (settings_open) {
                settings_open = false;
                ui.needs_redraw = true;
            } else if (notice_open) {
                notice_open = false;
                notice_item = -1;
                ui.needs_redraw = true;
            } else if (ui.menu_open) {
                ui.menu_open = false;
                ui.hover_item = -1;
                ui.needs_redraw = true;
            }
        }
        return;
    }

    case INPUT_MOUSE_MOVE: {
        int hit = hit_menu_item(e->mouse.x, e->mouse.y);
        u32 bar_h = taskbar_height();
        u32 bar_top = g_framebuffer.height > bar_h ? g_framebuffer.height - bar_h : 0;
        bool over_start = e->mouse.y >= (int)bar_top
                       && e->mouse.x < (int)START_BTN_W;
        if (hit != ui.hover_item || over_start) ui.needs_redraw = true;
        ui.hover_item = hit;
        return;
    }

    case INPUT_MOUSE_DOWN: {
        int mx = e->mouse.x, my = e->mouse.y;

        if (terminal_is_open()) {
            terminal_handle_mouse(mx, my);
            ui.needs_redraw = true;
            return;
        }
        if (files_is_open()) {
            files_handle_mouse(mx, my);
            ui.needs_redraw = true;
            return;
        }

        if (browser_is_open()) {
            browser_handle_mouse(mx, my);
            ui.needs_redraw = true;
            return;
        }

        if (settings_open) {
            settings_handle_click(mx, my);
            ui.needs_redraw = true;
            return;
        }

        if (notice_open) {
            /* The notice is modal: clicking its Close button or outside it
             * dismisses it, and the same click cannot activate an item below. */
            notice_open = false;
            notice_item = -1;
            ui.needs_redraw = true;
            return;
        }

        u32 bar_h = taskbar_height();
        u32 bar_top = g_framebuffer.height > bar_h ? g_framebuffer.height - bar_h : 0;
        bool on_taskbar = my >= (int)bar_top;

        if (on_taskbar) {
            int pinned = desktop_surface_taskbar_hit_test(mx, my, bar_top);
            if (pinned == 0) {
                ui.menu_open = false;
                browser_open();
                ui.needs_redraw = true;
                return;
            }
            if (pinned == 1) {
                ui.menu_open = false;
                notice_open = false;
                files_open();
                ui.needs_redraw = true;
                return;
            }
            if (pinned == 2) {
                ui.menu_open = false;
                notice_open = false;
                terminal_open();
                ui.needs_redraw = true;
                return;
            }
            if (pinned == 3) {
                ui.menu_open = false;
                notice_open = false;
                settings_open = true;
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
        if (shortcut >= 0) {
            ui.menu_open = false;
            notice_open = false;
            if (shortcut == 0) browser_open();
            else if (shortcut == 1) files_open();
            else if (shortcut == 2) terminal_open();
            else if (shortcut == 3) settings_open = true;
            ui.needs_redraw = true;
            return;
        }

        int hit = hit_menu_item(mx, my);
        if (hit >= 0) {
            ui.menu_open = false;
            ui.hover_item = -1;
            if (hit == 0) {
                notice_open = false;
                browser_open();
            } else if (hit == 1) {
                notice_open = false;
                terminal_open();
            } else if (hit == 2) {
                notice_open = false;
                files_open();
            } else if (hit == 3) {
                settings_open = true;
                notice_open = false;
            } else {
                notice_item = hit;
                notice_open = true;
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
        if (terminal_is_open()) {
            char key;
            while (keyboard_get_key(&key)) {
                terminal_handle_char(key);
                had_event = true;
            }
        }
        if (had_event) ui.needs_redraw = true;

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
