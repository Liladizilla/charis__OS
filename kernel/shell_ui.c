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
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/input.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/timer.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>
#include <kernel/printf.h>
#include <kernel/wallpaper.h>
#include <kernel/config.h>
#include <kernel/wizard.h>

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
#define TASKBAR_H     44
#define START_BTN_W   96
#define MENU_W        260
#define MENU_ITEM_H   34
#define CURSOR_BLINK  400   /* ms */

typedef struct {
    const char* label;
} menu_entry_t;

static const menu_entry_t MENU_ITEMS[] = {
    { "Terminal" },
    { "File Manager" },
    { "Text Editor" },
    { "Settings" },
    { "About CharisOS" },
};

static bool notice_open = false;
static int notice_item = -1;

static const char* const NOTICE_LINE_1[] = {
    "A graphical terminal is not wired up yet.",
    "The VFS exists, but the graphical browser is",
    "No graphical editor is implemented yet.",
    "Setup choices are temporary for this boot.",
    "CharisOS is a from-scratch x86_64 kernel.",
};
static const char* const NOTICE_LINE_2[] = {
    "Kernel output remains on the text/serial console.",
    "not connected to file operations yet.",
    "This screen will not pretend to show source code.",
    "Configuration-file persistence is not implemented.",
    "The desktop and built-in apps are still partial.",
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
    u32 y = h - TASKBAR_H;

    fb_fill_rect(0, y, w, TASKBAR_H, T_TASKBAR);
    fb_fill_rect(0, y, w, 1, T_BORDER);

    /* Start button. */
    bool hovered = input_get_mouse_y() >= (int)y && input_get_mouse_x() < (int)START_BTN_W;
    u32 bg = ui.menu_open ? T_MENU_HOVER
            : hovered     ? T_START_HOVER : T_START_BTN;
    fb_fill_rect(0, y, START_BTN_W, TASKBAR_H, bg);
    fb_draw_rect(0, y, START_BTN_W, TASKBAR_H, T_BORDER);

    /* "Charis" label, vertically centred in the cell. */
    u32 ty = y + (TASKBAR_H - font_cell_h) / 2;
    gfx_draw_string(14, ty, "Charis", T_FG, FB_TRANSPARENT);

    /* The timer provides uptime, not wall-clock time. Label it explicitly
     * and use one unambiguous HH:MM:SS format. */
    u64 secs = timer_get_ms() / 1000;
    char clock[12];
    clock[0] = 'U'; clock[1] = 'P'; clock[2] = ' ';
    clock[3] = '0' + (char)((secs / 36000) % 10);
    clock[4] = '0' + (char)((secs / 3600) % 10);
    clock[5] = ':';
    clock[6] = '0' + (char)((secs / 600) % 10);
    clock[7] = '0' + (char)((secs / 60) % 10);
    clock[8] = ':';
    clock[9] = '0' + (char)((secs / 10) % 10);
    clock[10] = '0' + (char)(secs % 10);
    clock[11] = 0;
    u32 cw = 11 * 8;
    gfx_draw_string(w - cw - 16, ty, clock, T_FG_DIM, FB_TRANSPARENT);
}

static void draw_menu(void) {
    if (!ui.menu_open) return;

    u32 h = g_framebuffer.height;
    u32 menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + 16;
    u32 y = h - TASKBAR_H - menu_h;
    u32 x = 0;

    fb_fill_rect(x + 4, y + 4, MENU_W, menu_h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, MENU_W, menu_h, T_MENU);
    fb_draw_rect(x, y, MENU_W, menu_h, T_BORDER);

    for (u32 i = 0; i < MENU_ITEM_COUNT; i++) {
        u32 iy = y + 8 + i * MENU_ITEM_H;
        bool hovered = (ui.hover_item == (int)i);
        fb_fill_rect(x + 2, iy, MENU_W - 4, MENU_ITEM_H,
                     hovered ? T_MENU_HOVER : T_MENU_ITEM);
        gfx_draw_string(x + 14, iy + (MENU_ITEM_H - font_cell_h) / 2,
                        MENU_ITEMS[i].label, T_FG, FB_TRANSPARENT);
    }
}

/* A launcher item must not silently send output to an invisible VGA console.
 * Until each application has a real framebuffer UI, explain its current state
 * in a consistent, dismissible panel instead of pretending it launched. */
static void draw_notice(void) {
    if (!notice_open || notice_item < 0 || notice_item >= (int)MENU_ITEM_COUNT) return;

    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 440, ch = 152;
    u32 x = sw > cw ? (sw - cw) / 2 : 0;
    u32 y = sh > ch ? (sh - ch) / 2 : 0;

    fb_fill_rect(x + 4, y + 4, cw, ch, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, cw, ch, T_MENU);
    fb_draw_rect(x, y, cw, ch, T_BORDER);
    fb_fill_rect(x, y, cw, 28, T_START_BTN);
    fb_fill_rect(x, y, cw, 1, CHARIS_UI_BORDER_STRONG);
    gfx_draw_string(x + 16, y + 7, MENU_ITEMS[notice_item].label, T_FG, FB_TRANSPARENT);

    gfx_draw_string(x + 20, y + 48, NOTICE_LINE_1[notice_item], T_FG, FB_TRANSPARENT);
    gfx_draw_string(x + 20, y + 72, NOTICE_LINE_2[notice_item], T_FG_DIM, FB_TRANSPARENT);

    u32 bw = 88, bh = 28;
    u32 bx = x + cw - bw - 16, by = y + ch - bh - 12;
    fb_fill_rect(bx, by, bw, bh, CHARIS_UI_ACCENT_SURFACE);
    fb_draw_rect(bx, by, bw, bh, CHARIS_UI_BORDER_STRONG);
    gfx_draw_string(bx + 24, by + 6, "Close", T_FG, FB_TRANSPARENT);
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
    draw_taskbar();
    draw_menu();
    draw_notice();
    ui_draw_cursor();
}

/* ── Hit testing ──────────────────────────────────────────────────── */

static int hit_menu_item(int mx, int my) {
    if (!ui.menu_open) return -1;
    u32 h = g_framebuffer.height;
    u32 menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + 16;
    u32 top = h - TASKBAR_H - menu_h;

    if ((u32)mx >= MENU_W) return -1;
    if (my < (int)top + 8) return -1;

    u32 rel = (u32)my - top - 8;
    if (rel >= MENU_ITEM_H * MENU_ITEM_COUNT) return -1;
    return (int)(rel / MENU_ITEM_H);
}

/* ── Input plumbing ───────────────────────────────────────────────── */

/* Runs in the keyboard IRQ: push the scancode as an event and return. */
static void ui_key_hook(u8 scancode, bool pressed) {
    input_event_t evt;
    evt.type = pressed ? INPUT_KEY_DOWN : INPUT_KEY_UP;
    evt.key.scancode = scancode;
    evt.key.keycode = 0;
    evt.timestamp = timer_get_ms();
    input_push_event(&evt);
}

void ui_install_input_hooks(void) {
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

        /* Escape dismisses the topmost transient surface. */
        if (e->type == INPUT_KEY_DOWN && base == KEY_ESCAPE) {
            if (notice_open) {
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
        bool over_start = e->mouse.y >= (int)(g_framebuffer.height - TASKBAR_H)
                       && e->mouse.x < (int)START_BTN_W;
        if (hit != ui.hover_item || over_start) ui.needs_redraw = true;
        ui.hover_item = hit;
        return;
    }

    case INPUT_MOUSE_DOWN: {
        int mx = e->mouse.x, my = e->mouse.y;

        if (notice_open) {
            /* The notice is modal: clicking its Close button or outside it
             * dismisses it, and the same click cannot activate an item below. */
            notice_open = false;
            notice_item = -1;
            ui.needs_redraw = true;
            return;
        }

        bool on_taskbar = my >= (int)(g_framebuffer.height - TASKBAR_H);

        if (on_taskbar && mx < (int)START_BTN_W) {
            ui.menu_open = !ui.menu_open;
            ui.hover_item = -1;
            ui.needs_redraw = true;
            return;
        }

        int hit = hit_menu_item(mx, my);
        if (hit >= 0) {
            ui.menu_open = false;
            ui.hover_item = -1;
            notice_item = hit;
            notice_open = true;
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

        asm volatile("hlt");
    }
}
