/* terminal_ui.c - Small native command terminal for the CharisOS desktop.
 *
 * This is a real framebuffer application, not a notice that redirects users
 * to the VGA console. Commands are intentionally read-only except for clear
 * and echo until process isolation and a full userspace shell are ready.
 */
#include <kernel/terminal_ui.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/ui_theme.h>
#include <kernel/keyboard.h>
#include <kernel/string.h>
#include <kernel/timer.h>
#include <kernel/pmm.h>
#include <kernel/diagnostics.h>
#include <kernel/config.h>
#include <kernel/serial.h>

#define TERM_INPUT_MAX  96
#define TERM_LINE_MAX   96
#define TERM_HISTORY    16

static bool term_visible;
static char term_input[TERM_INPUT_MAX];
static u32 term_input_len;
static char term_lines[TERM_HISTORY][TERM_LINE_MAX];
static u32 term_line_count;

static u32 term_px(u32 logical) { return gfx_ui_scale(logical); }
static u32 term_text_h(void) { return font_cell_h * gfx_text_get_scale(); }

static void term_number(char* out, u32 value) {
    char reversed[10];
    u32 count = 0;
    do {
        reversed[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value && count < sizeof(reversed));
    u32 i = 0;
    while (count) out[i++] = reversed[--count];
    out[i] = 0;
}

static void term_add_line(const char* text) {
    if (!text) return;
    if (term_line_count >= TERM_HISTORY) {
        for (u32 i = 1; i < TERM_HISTORY; i++)
            kstrncpy(term_lines[i - 1], term_lines[i], TERM_LINE_MAX - 1);
        term_line_count = TERM_HISTORY - 1;
    }
    kstrncpy(term_lines[term_line_count], text, TERM_LINE_MAX - 1);
    term_lines[term_line_count][TERM_LINE_MAX - 1] = 0;
    term_line_count++;
}

static void term_add_value(const char* label, u32 value, const char* suffix) {
    char line[TERM_LINE_MAX];
    char number[12];
    usize pos = 0;
    while (label && *label && pos + 1 < sizeof(line)) line[pos++] = *label++;
    term_number(number, value);
    for (usize i = 0; number[i] && pos + 1 < sizeof(line); i++) line[pos++] = number[i];
    while (suffix && *suffix && pos + 1 < sizeof(line)) line[pos++] = *suffix++;
    line[pos] = 0;
    term_add_line(line);
}

static void term_execute(void) {
    term_input[term_input_len] = 0;
    if (!term_input_len) return;

    char echoed[TERM_LINE_MAX];
    kstrncpy(echoed, "charisos> ", sizeof(echoed) - 1);
    usize prefix = kstrlen(echoed);
    kstrncpy(echoed + prefix, term_input, sizeof(echoed) - prefix - 1);
    term_add_line(echoed);
    serial_printf("TERMINAL_CMD %s\n", term_input);

    if (kstrcmp(term_input, "help") == 0) {
        term_add_line("help  clear  echo TEXT  uname  uptime");
        term_add_line("mem  tasks  settings  version");
    } else if (kstrcmp(term_input, "clear") == 0) {
        term_line_count = 0;
    } else if (kstrcmp(term_input, "uname") == 0 ||
               kstrcmp(term_input, "version") == 0) {
        term_add_line("CharisOS 1.0 x86_64");
        term_add_line("Freestanding C + NASM kernel");
    } else if (kstrcmp(term_input, "uptime") == 0) {
        u64 seconds = timer_get_ms() / 1000;
        term_add_value("Uptime: ", (u32)seconds, " seconds");
    } else if (kstrcmp(term_input, "mem") == 0) {
        term_add_value("Managed RAM used: ", pmm_used_pages() / 256, " MiB");
        term_add_value("Managed RAM free: ", pmm_free_pages() / 256, " MiB");
        term_add_value("Managed RAM total: ", pmm_total_pages() / 256, " MiB");
    } else if (kstrcmp(term_input, "tasks") == 0) {
        system_stats_t stats;
        diag_collect_stats(&stats);
        term_add_value("Tasks active: ", stats.processes_active, "");
        term_add_value("Tasks total: ", stats.processes_total, "");
        term_add_value("Tasks blocked: ", stats.processes_blocked, "");
        term_add_value("Context switches: ", stats.context_switches, "");
    } else if (kstrcmp(term_input, "settings") == 0) {
        static const char* const styles[] = { "Proportional", "Monospace", "Bitmap serif" };
        int style = config_get_int("font_style", 0);
        if (style < 0 || style > 2) style = 0;
        term_add_line("Display preferences:");
        term_add_line(styles[style]);
        term_add_value("Font scale: ", (u32)config_get_int("font_scale", 1), "x");
        term_add_value("UI scale: ", (u32)config_get_int("ui_scale", 100), "%");
        term_add_line(config_is_persistent() ? "Storage: CHARISOS.CFG" : "Storage: session only");
    } else if (kstrncmp(term_input, "echo ", 5) == 0) {
        term_add_line(term_input + 5);
    } else {
        term_add_line("Command not found. Type help.");
    }

    term_input_len = 0;
    term_input[0] = 0;
}

void terminal_open(void) {
    char discard;
    while (keyboard_get_key(&discard)) { }
    if (!term_line_count) {
        term_add_line("CharisOS native terminal");
        term_add_line("Type help to list available commands.");
    }
    term_visible = true;
    serial_puts("TERMINAL_OPEN");
}

void terminal_close(void) {
    term_visible = false;
    term_input_len = 0;
    term_input[0] = 0;
}

bool terminal_is_open(void) {
    return term_visible;
}

void terminal_handle_char(char ch) {
    if (!term_visible) return;
    if (ch == '\n' || ch == '\r') {
        term_execute();
        return;
    }
    if (ch == '\b') {
        if (term_input_len) term_input[--term_input_len] = 0;
        return;
    }
    if ((u8)ch < 0x20u || (u8)ch > 0x7Eu) return;
    if (term_input_len + 1 < sizeof(term_input)) {
        term_input[term_input_len++] = ch;
        term_input[term_input_len] = 0;
    }
}

static void term_geometry(u32* x, u32* y, u32* w, u32* h, u32* title_h) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    *w = sw > term_px(760) ? term_px(760) : (sw > term_px(24) ? sw - term_px(24) : sw);
    *h = sh > term_px(520) ? term_px(520) : (sh > term_px(24) ? sh - term_px(24) : sh);
    *x = sw > *w ? (sw - *w) / 2 : 0;
    *y = sh > *h ? (sh - *h) / 2 : 0;
    u32 base = term_px(36), needed = term_text_h() + term_px(12);
    *title_h = base > needed ? base : needed;
}

bool terminal_handle_mouse(int mx, int my) {
    if (!term_visible) return false;
    u32 x, y, w, h, title_h;
    term_geometry(&x, &y, &w, &h, &title_h);
    if (mx >= (int)(x + w - term_px(36)) && mx < (int)(x + w) &&
        my >= (int)y && my < (int)(y + title_h)) {
        terminal_close();
    }
    return true;
}

void terminal_render(void) {
    if (!term_visible || !g_framebuffer.initialized) return;

    u32 x, y, w, h, title_h;
    term_geometry(&x, &y, &w, &h, &title_h);
    u32 text_h = term_text_h();
    u32 line_h = text_h + term_px(8);
    u32 prompt_h = text_h + term_px(20);
    u32 content_top = y + title_h + term_px(12);
    u32 content_bottom = y + h - prompt_h - term_px(12);
    u32 rows = content_bottom > content_top ? (content_bottom - content_top) / line_h : 1;
    if (!rows) rows = 1;

    fb_fill_rect(x + term_px(5), y + term_px(5), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER);
    fb_fill_rect(x, y, w, title_h, CHARIS_UI_PANEL_RAISED);
    gfx_draw_string(x + term_px(16), y + (title_h - text_h) / 2,
                    "TERMINAL", CHARIS_UI_TEXT, FB_TRANSPARENT);

    u32 close_w = term_px(28);
    fb_fill_rect(x + w - close_w - term_px(4), y + term_px(4),
                 close_w, title_h - term_px(8), CHARIS_UI_SURFACE);
    fb_draw_rect(x + w - close_w - term_px(4), y + term_px(4),
                 close_w, title_h - term_px(8), CHARIS_UI_BORDER);
    gfx_draw_string(x + w - close_w + term_px(5),
                    y + (title_h - text_h) / 2, "x",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);

    u32 first = term_line_count > rows ? term_line_count - rows : 0;
    for (u32 i = first; i < term_line_count; i++) {
        u32 row = i - first;
        gfx_draw_string(x + term_px(18), content_top + row * line_h,
                        term_lines[i], CHARIS_UI_TEXT, FB_TRANSPARENT);
    }

    u32 prompt_y = y + h - prompt_h - term_px(8);
    fb_fill_rect(x + term_px(12), prompt_y, w - term_px(24),
                 prompt_h, CHARIS_UI_SURFACE);
    gfx_draw_string(x + term_px(20), prompt_y + (prompt_h - text_h) / 2,
                    "charisos> ", CHARIS_UI_ACCENT, FB_TRANSPARENT);
    u32 prompt_width = 0;
    gfx_text_size("charisos> ", &prompt_width, NULL);
    gfx_draw_string(x + term_px(20) + prompt_width,
                    prompt_y + (prompt_h - text_h) / 2,
                    term_input, CHARIS_UI_TEXT, FB_TRANSPARENT);
}
