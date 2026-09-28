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
#include <kernel/printf.h>
#include <kernel/shell.h>
#include <kernel/wallpaper.h>
#include <kernel/config.h>
#include <kernel/wizard.h>
#include <kernel/string.h>

/* ── Theme ────────────────────────────────────────────────────────── */
#define T_BG          FB_ARGB(0xFF, 0x0B, 0x0F, 0x17)
#define T_TASKBAR     FB_ARGB(0xEE, 0x15, 0x1B, 0x26)
#define T_START_BTN   FB_ARGB(0xEE, 0x20, 0x28, 0x38)
#define T_START_HOVER FB_ARGB(0xFF, 0x2C, 0x37, 0x4C)
#define T_MENU        FB_ARGB(0xF5, 0x16, 0x1D, 0x29)
#define T_MENU_ITEM   FB_ARGB(0xF5, 0x1E, 0x27, 0x36)
#define T_MENU_HOVER  FB_ARGB(0xFF, 0x2E, 0x6B, 0xD6)
#define T_FG          FB_ARGB(0xFF, 0xE6, 0xEC, 0xF5)
#define T_FG_DIM      FB_ARGB(0xFF, 0x93, 0x9F, 0xB5)
#define T_BORDER      FB_ARGB(0xFF, 0x2A, 0x33, 0x45)
#define T_CURSOR      FB_ARGB(0xFF, 0xFF, 0xFF, 0xFF)

/* Layout. */
#define TASKBAR_H     44
#define START_BTN_W   96
#define MENU_W        260
#define MENU_ITEM_H   34
#define CURSOR_BLINK  400   /* ms */

typedef struct {
    const char* label;
    const char* command;   /* what the shell runs when picked */
} menu_entry_t;

static const menu_entry_t MENU_ITEMS[] = {
    { "Terminal",        "terminal" },
    { "File Manager",    "files"    },
    { "Text Editor",     "editor"   },
    { "Settings",        "settings" },
    { "About CharisOS",  "about"    },
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

    /* Right side: a clock. The kernel has a running tick counter, which is
     * uptime rather than wall-clock time, so it is labelled as such. */
    u64 secs = timer_get_ms() / 1000;
    char clock[32];
    clock[0] = '0' + (char)((secs / 36000) % 10);
    clock[1] = '0' + (char)((secs / 3600) % 10);
    clock[2] = ':';
    clock[3] = '0' + (char)((secs / 600) % 10);
    clock[4] = '0' + (char)((secs / 60) % 10);
    clock[5] = '0' + (char)((secs / 10) % 10);
    clock[6] = '0' + (char)(secs % 10);
    clock[7] = 'u';   /* uptime, not time of day */
    clock[8] = 0;
    u32 cw = 8 * 8;   /* 8 glyphs * 8px */
    gfx_draw_string(w - cw - 16, ty, clock, T_FG_DIM, FB_TRANSPARENT);
}

static void draw_menu(void) {
    if (!ui.menu_open) return;

    u32 h = g_framebuffer.height;
    u32 menu_h = MENU_ITEM_H * MENU_ITEM_COUNT + 16;
    u32 y = h - TASKBAR_H - menu_h;
    u32 x = 0;

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

        /* Escape closes the menu. */
        if (e->type == INPUT_KEY_DOWN && base == KEY_ESCAPE && ui.menu_open) {
            ui.menu_open = false;
            ui.hover_item = -1;
            ui.needs_redraw = true;
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
            ui.needs_redraw = true;
            shell_execute((char*)MENU_ITEMS[hit].command);
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
