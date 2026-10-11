/* wizard.c - First-run setup flow
 *
 * Shown once, on a machine that has never completed setup, when a linear
 * framebuffer is available. It collects the choices a desktop OS normally asks
 * for, writes them to the config store, and hands control to the shell.
 *
 * Being honest about what is not wired matters more here than anywhere else.
 * A first-run wizard that quietly pretends to partition a disk or "configure"
 * a network it cannot reach would be worse than one that says so. Where a step
 * cannot do the real thing it says that in as many words, and the choice still
 * gets recorded so the intent is not lost.
 *
 * Input: the same queue the desktop drains. This is a blocking flow, so it
 * calls input_process_events() itself rather than relying on a render loop.
 */

#include <kernel/wizard.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/input.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/net.h>
#include <kernel/config.h>
#include <kernel/ata.h>
#include <kernel/wallpaper.h>
#include <kernel/shell_ui.h>
#include <kernel/timer.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/ui_theme.h>
#include <kernel/scheduler.h>

/* ── Shared solid-morphism palette ────────────────────────────────────── */
#define W_BG       CHARIS_UI_BACKGROUND
#define W_CARD     CHARIS_UI_PANEL
#define W_BORDER   CHARIS_UI_BORDER
#define W_FG       CHARIS_UI_TEXT
#define W_DIM      CHARIS_UI_MUTED
#define W_ACCENT   CHARIS_UI_ACCENT
#define W_SEL      CHARIS_UI_ACCENT_SURFACE
#define W_HINT     CHARIS_UI_MUTED

/* ── Option lists ─────────────────────────────────────────────────────── */

static const char* const LANGUAGES[] = {
    "English (United States)",
    "English (United Kingdom)",
    "English (Kenya)",
    "Français",
    "Español",
    "Deutsch",
    "Italiano",
    "Português (Brasil)",
    "Kiswahili",
};
#define LANGUAGE_COUNT ((u32)(sizeof(LANGUAGES) / sizeof(LANGUAGES[0])))

/* Offsets rather than zone names: the kernel has no timezone database, so
 * pretending to know about "Europe/London" would be a lie. The offset is real
 * and usable. */
static const char* const TIMEZONES[] = {
    "UTC-12:00  Fixed offset",
    "UTC-08:00  Fixed offset",
    "UTC-05:00  Fixed offset",
    "UTC-03:00  Fixed offset",
    "UTC+00:00  Fixed offset",
    "UTC+01:00  Fixed offset",
    "UTC+03:00  East Africa",
    "UTC+05:30  India",
    "UTC+08:00  Singapore / Beijing",
    "UTC+09:00  Tokyo / Seoul",
    "UTC+12:00  Fixed offset",
};
#define TIMEZONE_COUNT ((u32)(sizeof(TIMEZONES) / sizeof(TIMEZONES[0])))

/* ── Steps ────────────────────────────────────────────────────────────── */

typedef enum {
    STEP_WELCOME = 0,
    STEP_MODE,
    STEP_LANGUAGE,
    STEP_TIMEZONE,
    STEP_DISK,
    STEP_NETWORK,
    STEP_WALLPAPER,
    STEP_DONE,
    STEP_COUNT
} wizard_step_t;

typedef struct {
    wizard_step_t step;
    u32 sel[STEP_COUNT];     /* highlighted row per step */
    u32 language;
    u32 timezone;
    u32 wallpaper;
    bool install;            /* true = install to disk, false = run from device */
    bool network_static;
    u8  ip[4];
    bool finished;           /* user pressed Apply, not Skip */
} wizard_state_t;

static wizard_state_t w;

/* ── Chrome ───────────────────────────────────────────────────────────── */

static void card(u32 x, u32 y, u32 w, u32 h) {
    /* Hard offset shadow: depth without blur or alpha blending. */
    fb_fill_rect(x + 4, y + 4, w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, W_CARD);
    fb_draw_rect(x, y, w, h, W_BORDER);
}

static void text(u32 x, u32 y, const char* s, u32 colour) {
    gfx_draw_string(x, y, s, colour, FB_TRANSPARENT);
}

/* Draw a list, with the selected row highlighted. Returns the y of the first
 * row so hit-testing and drawing cannot disagree about the geometry. */
static u32 draw_list(u32 x, u32 y, u32 w, const char* const* items, u32 count, u32 selected) {
    const u32 row_h = font_cell_h + 10;
    for (u32 i = 0; i < count; i++) {
        u32 ry = y + i * row_h;
        bool on = (i == selected);
        if (on) fb_fill_rect(x, ry, w, row_h, W_SEL);
        u32 fg = on ? W_ACCENT : W_FG;
        gfx_draw_string(x + 14, ry + 5, items[i], fg, FB_TRANSPARENT);
    }
    return row_h;
}

static u32 list_row_height(void) { return font_cell_h + 10; }

/* Which row is under the pointer, or -1. Same geometry as draw_list(). */
static int hit_list(u32 x, u32 y, u32 w, u32 count) {
    int mx = input_get_mouse_x();
    int my = input_get_mouse_y();
    if (mx < (int)x || mx >= (int)(x + w)) return -1;
    u32 row_h = list_row_height();
    if (my < (int)y) return -1;
    u32 rel = (u32)my - y;
    if (rel >= row_h * count) return -1;
    return (int)(rel / row_h);
}

static void button(u32 x, u32 y, u32 w, u32 h, const char* label, bool primary) {
    int mx = input_get_mouse_x();
    int my = input_get_mouse_y();
    bool hot = mx >= (int)x && mx < (int)(x + w) && my >= (int)y && my < (int)(y + h);
    fb_fill_rect(x + 2, y + 2, w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, primary ? W_SEL : W_CARD);
    fb_draw_rect(x, y, w, h, hot ? W_ACCENT : W_BORDER);
    u32 tw = 8 * (u32)kstrlen(label);
    gfx_draw_string(x + (w > tw ? (w - tw) / 2 : 0), y + (h - font_cell_h) / 2,
                    label, primary ? W_ACCENT : W_FG, FB_TRANSPARENT);
}

static bool hit(u32 x, u32 y, u32 w, u32 h) {
    int mx = input_get_mouse_x();
    int my = input_get_mouse_y();
    return mx >= (int)x && mx < (int)(x + w) && my >= (int)y && my < (int)(y + h);
}

/* ── Per-step drawing ─────────────────────────────────────────────────── */

static void draw_welcome(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 320;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    text(cx + 40, cy + 36, "Welcome to CharisOS", W_FG);
    text(cx + 40, cy + 64, "This is the first run on this machine.", W_DIM);
    text(cx + 40, cy + 84, "Answer a few questions to set the system up.", W_DIM);
    text(cx + 40, cy + 120, "You can change any of this later from Settings.", W_HINT);

    button(cx + 40, cy + ch - 76, 200, 44, "Begin setup", true);
    button(cx + 260, cy + ch - 76, 160, 44, "Use defaults", false);
}

static void draw_step_header(const char* title, const char* hint, u32 cx, u32 cy) {
    text(cx + 40, cy + 32, title, W_FG);
    text(cx + 40, cy + 58, hint, W_DIM);
}

static void draw_mode(void) {
    static const char* const items[] = {
        "Live session from the boot medium",
    };
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 340;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    draw_step_header("Live session",
                     "An installer is not available in this build.", cx, cy);
    draw_list(cx + 40, cy + 110, cw - 80, items, 1, w.sel[STEP_MODE]);

    text(cx + 40, cy + 210,
         "There is no installer yet; this session runs from the boot medium.",
         W_HINT);
    text(cx + 40, cy + 230,
         "Setup persists only when an existing writable config file is present.",
         W_HINT);
}

static void draw_language(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 460;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    draw_step_header("Language", "Used for system messages and the default locale.",
                     cx, cy);
    draw_list(cx + 40, cy + 100, cw - 80, LANGUAGES, LANGUAGE_COUNT, w.language);

    text(cx + 40, cy + ch - 44,
         "Keyboard layout is US QWERTY for now; other layouts are not mapped yet.",
         W_HINT);
}

static void draw_timezone(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 470;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    draw_step_header("Time zone", "The kernel has no timezone database, so this is an offset.",
                     cx, cy);
    draw_list(cx + 40, cy + 100, cw - 80, TIMEZONES, TIMEZONE_COUNT, w.timezone);
}

static void draw_disk(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 320;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    draw_step_header("Target drive", "Where the system would be installed.",
                     cx, cy);

    text(cx + 40, cy + 110,
         ata_is_present() ? "Primary ATA device detected." : "No primary ATA device detected.",
         W_ACCENT);
    text(cx + 40, cy + 140,
         "The driver supports legacy ATA PIO on the primary channel only.",
         W_DIM);
    text(cx + 40, cy + 160,
         "It does not provide SATA/AHCI or NVMe support.",
         W_DIM);
    text(cx + 40, cy + 190,
         "Disk installation is not implemented; this step is informational.",
         W_HINT);
    text(cx + 40, cy + 210,
         "This screen reports detection only and will not modify a disk.",
         W_HINT);
}

static void draw_network(void) {
    static const char* const items[] = {
        "Automatic (DHCP)",
        "Manual address",
    };
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 400;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    net_interface_t* ni = net_get_interface();
    draw_step_header("Network", "Connect this machine to a network.",
                     cx, cy);

    if (ni && ni->initialized) {
        text(cx + 40, cy + 96, "Adapter: RTL8139, initialised.", W_DIM);
    } else {
        text(cx + 40, cy + 96, "Network interface is not initialized.", W_HINT);
        text(cx + 40, cy + 116,
             "The current network driver supports RTL8139 only.", W_HINT);
    }

    draw_list(cx + 40, cy + 150, cw - 80, items, 2, w.sel[STEP_NETWORK]);

    if (w.sel[STEP_NETWORK] == 0) {
        text(cx + 40, cy + 250,
             "DHCP is not implemented yet, so no address will be fetched.",
             W_HINT);
        text(cx + 40, cy + 270,
             "The choice is recorded for when it is.", W_HINT);
    } else {
        text(cx + 40, cy + 250, "No address is configured.", W_DIM);
        text(cx + 40, cy + 270,
             "A static address is recorded but not yet applied to the driver.",
             W_HINT);
    }
}

/* The wallpaper step previews full-screen: the chosen style is drawn behind
 * everything, and the card sits on top with its name. Seeing the whole desktop
 * is the point, so there is nothing to clip. */
static void draw_wallpaper(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 420, ch = 96;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = sh > 200 ? sh - 200 : 0;

    card(cx, cy, cw, ch);
    text(cx + 20, cy + 18, wallpaper_name(w.wallpaper), W_FG);
    text(cx + 20, cy + 42, wallpaper_description(w.wallpaper), W_DIM);
    text(cx + 20, cy + 68, "Click this panel to cycle styles.", W_HINT);
}

static void draw_done(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 cw = 640, ch = 300;
    u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
    u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
    card(cx, cy, cw, ch);

    text(cx + 40, cy + 40, "Setup complete", W_ACCENT);
    text(cx + 40, cy + 78, "Summary:", W_DIM);

    text(cx + 40, cy + 106, LANGUAGES[w.language], W_FG);
    text(cx + 40, cy + 128, TIMEZONES[w.timezone], W_FG);
    text(cx + 40, cy + 150, wallpaper_name(w.wallpaper), W_FG);
    text(cx + 40, cy + 172, w.install ? "install to disk" : "run from device", W_FG);

    text(cx + 40, cy + 200,
         "Config can update an existing writable file, but cannot create",
         W_HINT);
    text(cx + 40, cy + 220,
         "a missing config file. Persistence depends on that file existing.",
         W_HINT);
}

/* ── Footer ───────────────────────────────────────────────────────────── */

static void draw_footer(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    if (sh < 60 || w.step == STEP_WELCOME) return;

    /* One consistent navigation bar, with exactly one primary action. */
    u32 y = sh - 48;
    fb_fill_rect(0, y, sw, 48, W_BG);
    fb_fill_rect(0, y, sw, 1, W_BORDER);
    button(sw > 228 ? sw - 228 : 0, y + 8, 88, 32, "Back", false);
    button(sw > 128 ? sw - 128 : 0, y + 8, 112, 32,
           w.step == STEP_DONE ? "Finish" : "Continue", true);
}

/* ── Input handling ───────────────────────────────────────────────────── */

static void apply_and_save(void) {
    config_set_int("language", (int)w.language);
    config_set_int("timezone", (int)w.timezone);
    config_set_string("timezone_name", TIMEZONES[w.timezone]);
    config_set_int("wallpaper", (int)w.wallpaper);
    config_set_string("wallpaper_name", wallpaper_name(w.wallpaper));
    config_set_bool("install_to_disk", w.install);
    config_set_bool("net_dhcp", !w.network_static);
    config_set_bool("net_enabled", true);
    config_set_bool("setup_complete", true);
    config_save("/etc/charisos.conf");
}

static void step_leave(wizard_step_t from) {
    switch (from) {
    case STEP_WELCOME: break;                 /* nothing to record */
    case STEP_MODE:      w.install = false; break;
    case STEP_LANGUAGE:  w.language = w.sel[STEP_LANGUAGE]; break;
    case STEP_TIMEZONE:  w.timezone = w.sel[STEP_TIMEZONE]; break;
    case STEP_DISK:      break;               /* informational */
    case STEP_NETWORK:   w.network_static = (w.sel[STEP_NETWORK] == 1); break;
    case STEP_WALLPAPER: w.wallpaper = w.sel[STEP_WALLPAPER]; break;
    case STEP_DONE:      apply_and_save(); w.finished = true; break;
    default: break;
    }
}

static void advance(void) {
    if (w.step == STEP_WELCOME) {
        /* "Begin setup" goes to the first real question. Jumping to the end
         * here was wrong: it made the primary button behave exactly like
         * "Use defaults" and skipped the entire flow. */
        w.step = STEP_MODE;
    } else if (w.step == STEP_DONE) {
        step_leave(STEP_DONE);
    } else {
        step_leave(w.step);
        wizard_step_t next = (wizard_step_t)(w.step + 1);
        /* The disk step only makes sense when installing. */
        if (next == STEP_DISK && !w.install) next = STEP_NETWORK;
        w.step = next;
    }
}

static void go_back(void) {
    if (w.step == STEP_WELCOME) return;
    if (w.step == STEP_DONE) { w.step = STEP_WALLPAPER; return; }
    wizard_step_t prev = (wizard_step_t)(w.step - 1);
    /* The install-only disk step is skipped in both directions. Returning
     * from Network should land on Time zone, not jump back to Mode. */
    if (w.step == STEP_NETWORK && !w.install) prev = STEP_TIMEZONE;
    w.step = prev;
}

static void adjust(int delta) {
    u32 count = 0;
    switch (w.step) {
    case STEP_MODE:      count = 1; break;
    case STEP_LANGUAGE:  count = LANGUAGE_COUNT; break;
    case STEP_TIMEZONE:  count = TIMEZONE_COUNT; break;
    case STEP_DISK:      return;
    case STEP_NETWORK:   count = 2; break;
    case STEP_WALLPAPER: count = wallpaper_count(); break;
    case STEP_WELCOME:
    case STEP_DONE:
    default:             return;
    }
    u32 s = w.sel[w.step];
    if (delta > 0) s = (s + 1) % count;
    else           s = (s + count - 1) % count;
    w.sel[w.step] = s;
}

static bool handle_event(const input_event_t* e) {
    switch (e->type) {
    case INPUT_KEY_DOWN: {
        u8 code = e->key.scancode;
        bool ext = (code & 0x80) != 0;
        u8 base = code & 0x7F;

        if (ext) return false;               /* Super and friends: not ours */

        switch (base) {
        case KEY_UP:    adjust(-1); return true;
        case KEY_DOWN:  adjust(1);  return true;
        case KEY_ENTER: advance();  return true;
        case KEY_ESCAPE:go_back();  return true;
        default: return false;
        }
    }

    case INPUT_MOUSE_MOVE: {
        /* Hovering a row moves the selection, which is what a list should do. */
        u32 x, y, cw;
        switch (w.step) {
        case STEP_MODE:     x = 40; y = 110; cw = 560; break;
        case STEP_LANGUAGE: x = 40; y = 100; cw = 560; break;
        case STEP_TIMEZONE: x = 40; y = 100; cw = 560; break;
        case STEP_NETWORK:  x = 40; y = 150; cw = 560; break;
        case STEP_WALLPAPER:x = 24; y = 60;  cw = 672; break;
        default: return false;
        }
        u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
        u32 card_w = (w.step == STEP_WALLPAPER) ? 720 : 640;
        u32 card_h = (w.step == STEP_MODE) ? 340 :
                     (w.step == STEP_NETWORK) ? 400 :
                     (w.step == STEP_LANGUAGE) ? 460 :
                     (w.step == STEP_TIMEZONE) ? 470 : 300;
        u32 cx = (sw > card_w) ? (sw - card_w) / 2 : 0;
        u32 cy = (sh > card_h) ? (sh - card_h) / 2 : 0;

        if (w.step == STEP_WALLPAPER) {
            /* Hover never changes the selected wallpaper. Use a click. */
            return true;
        }

        u32 count = (w.step == STEP_MODE) ? 1
                  : (w.step == STEP_NETWORK) ? 2
                  : (w.step == STEP_LANGUAGE) ? LANGUAGE_COUNT
                  : (w.step == STEP_TIMEZONE) ? TIMEZONE_COUNT : 0;
        if (!count) return false;
        int r = hit_list(cx + x, cy + y, cw, count);
        if (r >= 0) w.sel[w.step] = (u32)r;
        return true;
    }

    case INPUT_MOUSE_DOWN: {
        u32 sw = g_framebuffer.width, sh = g_framebuffer.height;

        if (w.step == STEP_WELCOME) {
            u32 cw = 640, ch = 320;
            u32 cx = (sw > cw) ? (sw - cw) / 2 : 0;
            u32 cy = (sh > ch) ? (sh - ch) / 2 : 0;
            if (hit(cx + 40, cy + ch - 76, 200, 44)) { advance(); return true; }
            if (hit(cx + 260, cy + ch - 76, 160, 44)) {
                /* "Use defaults" goes to the summary with the stated defaults. */
                w.step = STEP_DONE;
                return true;
            }
            return true;
        }

        /* Back and the single primary action are in the same footer on every
         * step. A random click no longer finishes setup by accident. */
        u32 footer_y = sh > 48 ? sh - 48 : 0;
        if (hit(sw > 228 ? sw - 228 : 0, footer_y + 8, 88, 32)) {
            go_back();
            return true;
        }
        if (hit(sw > 128 ? sw - 128 : 0, footer_y + 8, 112, 32)) {
            advance();
            return true;
        }
        if (w.step == STEP_DONE) return true;

        u32 card_w = 640, card_h = 320;
        u32 row_x = 40, row_y = 0, row_w = 560, row_count = 0;
        switch (w.step) {
        case STEP_MODE:      card_h = 340; row_y = 110; row_count = 1; break;
        case STEP_LANGUAGE:  card_h = 460; row_y = 100; row_count = LANGUAGE_COUNT; break;
        case STEP_TIMEZONE:  card_h = 470; row_y = 100; row_count = TIMEZONE_COUNT; break;
        case STEP_NETWORK:   card_h = 400; row_y = 150; row_count = 2; break;
        case STEP_WALLPAPER: {
            u32 cw = 420, ch = 96;
            u32 cx = sw > cw ? (sw - cw) / 2 : 0;
            u32 cy = sh > 200 ? sh - 200 : 0;
            if (hit(cx, cy, cw, ch)) adjust(1);
            return true;
        }
        default: return true;
        }

        u32 cx = sw > card_w ? (sw - card_w) / 2 : 0;
        u32 cy = sh > card_h ? (sh - card_h) / 2 : 0;
        int row = hit_list(cx + row_x, cy + row_y, row_w, row_count);
        if (row >= 0) w.sel[w.step] = (u32)row;
        return true;
    }

    default:
        return false;
    }
}

/* ── Flow ─────────────────────────────────────────────────────────────── */

bool wizard_is_first_boot(void) {
    return !config_get_bool("setup_complete", false);
}

bool wizard_run(void) {
    if (!g_framebuffer.initialized) return false;
    if (!wizard_is_first_boot()) return false;

    w.step = STEP_WELCOME;
    w.sel[STEP_MODE] = 0;
    w.sel[STEP_LANGUAGE] = 0;
    w.sel[STEP_TIMEZONE] = 6;        /* UTC+03:00, East Africa */
    w.sel[STEP_NETWORK] = 0;
    w.sel[STEP_WALLPAPER] = 0;
    w.language = 0;
    w.timezone = 6;
    w.wallpaper = 0;
    w.install = false;
    w.network_static = false;
    w.finished = false;

    kprintf("[WIZ] first run: starting setup\n");

    u32 last_w = 0, last_h = 0;
    wizard_step_t last_step = (wizard_step_t)-1;
    u32 last_sel = 0;
    int last_mx = -1, last_my = -1;
    bool first = true;

    while (!w.finished) {
        input_process_events();
        input_event_t e;
        while (input_pop_event(&e)) handle_event(&e);

        /* Repaint only when something actually changed. The wizard used to
         * redraw every pass, which is a lot of wasted work for a static screen
         * and means any screenshot of it catches a half-finished frame. */
        int mx = input_get_mouse_x();
        int my = input_get_mouse_y();
        u32 sel_now = w.sel[w.step];
        bool dirty = first
                  || w.step != last_step
                  || sel_now != last_sel
                  || mx != last_mx || my != last_my
                  || g_framebuffer.width  != last_w
                  || g_framebuffer.height != last_h;

        last_step = w.step;
        last_sel  = sel_now;
        last_mx = mx; last_my = my;
        last_w = g_framebuffer.width;
        last_h = g_framebuffer.height;
        first = false;

        if (!dirty) { scheduler_yield(); asm volatile("hlt"); continue; }

        /* Repaint the full background before each dirty frame so old
         * cards never ghost through after moving between setup steps. The
         * wallpaper step previews the currently selected solid style. */
        u32 preview_style = w.step == STEP_WALLPAPER
                          ? w.wallpaper
                          : (u32)config_get_int("wallpaper", 0);
        wallpaper_draw(preview_style);

        switch (w.step) {
        case STEP_WELCOME:  draw_welcome(); break;
        case STEP_MODE:     draw_mode(); break;
        case STEP_LANGUAGE: draw_language(); break;
        case STEP_TIMEZONE: draw_timezone(); break;
        case STEP_DISK:     draw_disk(); break;
        case STEP_NETWORK:  draw_network(); break;
        case STEP_WALLPAPER:draw_wallpaper(); break;
        case STEP_DONE:     draw_done(); break;
        default: break;
        }

        draw_footer();
        ui_draw_cursor();

        /* This wizard runs inside a cooperative task. Yield so the shell and
         * ring-3 test task can make progress while setup waits for input. */
        scheduler_yield();
        asm volatile("hlt");
    }

    kprintf("[WIZ] setup complete\n");
    return true;
}
