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
static bool term_bounds_valid;
static u32 term_bounds_x, term_bounds_y, term_bounds_w, term_bounds_h;

#define TERM_TEXT_SCALE 1u
static u32 term_px(u32 logical) { return gfx_ui_scale(logical); }
static u32 term_text_h(void) { return font_cell_h * gfx_text_get_scale() * TERM_TEXT_SCALE; }
static void term_draw(u32 x, u32 y, const char* s, u32 fg, u32 bg) {
    gfx_draw_string_scaled(x, y, s, fg, bg, TERM_TEXT_SCALE);
}

static u32 term_measure(const char* s) {
    u32 width = 0;
    gfx_text_size(s, &width, NULL);
    return width * TERM_TEXT_SCALE;
}

/* Return one visual line of text without splitting a word unless the word
 * itself is wider than the available canvas. */
static void term_next_segment(const char* line, u32 start, u32 max_width,
                              u32* end_out, u32* next_out) {
    u32 i = start, width = 0, last_space = start, width_at_space = 0;
    while (line[i]) {
        char one[2] = { line[i], 0 };
        u32 char_width = term_measure(one);
        if (width + char_width > max_width) break;
        width += char_width;
        if (line[i] == ' ') {
            last_space = i;
            width_at_space = width - char_width;
        }
        i++;
    }
    if (!line[i]) {
        *end_out = i;
        *next_out = i;
        return;
    }
    if (i == start) i++;
    if (last_space > start) {
        *end_out = last_space;
        *next_out = last_space + 1;
    } else {
        *end_out = i;
        *next_out = i;
    }
    (void)width_at_space;
}

static u32 term_wrapped_rows(const char* line, u32 max_width) {
    u32 rows = 0, pos = 0;
    if (!line || !line[0]) return 1;
    while (line[pos]) {
        u32 end = pos, next = pos;
        term_next_segment(line, pos, max_width, &end, &next);
        if (next <= pos) break;
        rows++;
        pos = next;
    }
    return rows ? rows : 1;
}

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

u32 terminal_titlebar_height(void) {
    u32 base = term_px(36), needed = term_text_h() + term_px(12);
    return base > needed ? base : needed;
}

void terminal_set_bounds(u32 x, u32 y, u32 width, u32 height) {
    if (!width || !height) { term_bounds_valid = false; return; }
    term_bounds_x = x; term_bounds_y = y;
    term_bounds_w = width; term_bounds_h = height;
    term_bounds_valid = true;
}

void terminal_reset_bounds(void) { term_bounds_valid = false; }

static void term_geometry(u32* x, u32* y, u32* w, u32* h, u32* title_h) {
    if (term_bounds_valid) {
        *x = term_bounds_x; *y = term_bounds_y;
        *w = term_bounds_w; *h = term_bounds_h;
        *title_h = terminal_titlebar_height();
        return;
    }
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    *w = sw > term_px(760) ? term_px(760) : (sw > term_px(24) ? sw - term_px(24) : sw);
    *h = sh > term_px(520) ? term_px(520) : (sh > term_px(24) ? sh - term_px(24) : sh);
    *x = sw > *w ? (sw - *w) / 2 : 0;
    *y = sh > *h ? (sh - *h) / 2 : 0;
    *title_h = terminal_titlebar_height();
}

bool terminal_handle_mouse(int mx, int my) {
    if (!term_visible) return false;
    u32 x, y, w, h, title_h;
    term_geometry(&x, &y, &w, &h, &title_h);
    if (mx < (int)x || mx >= (int)(x + w) ||
        my < (int)y || my >= (int)(y + h)) return false;
    if (my < (int)(y + title_h)) {
        if (mx >= (int)(x + w - term_px(36))) {
            terminal_close();
            return true;
        }
        return false; /* unclaimed titlebar space is the drag handle */
    }
    return true;
}

static u32 terminal_line_color(const char* line) {
    if (!line) return CHARIS_UI_TEXT;
    if (kstrncmp(line, "charisos> ", 10) == 0) return CHARIS_UI_SUCCESS;
    if (kstrncmp(line, "Command not found", 17) == 0) return CHARIS_UI_WARNING;
    if (kstrncmp(line, "CharisOS", 8) == 0) return CHARIS_UI_ACCENT;
    if (kstrncmp(line, "Managed RAM", 11) == 0 ||
        kstrncmp(line, "Tasks ", 6) == 0 ||
        kstrncmp(line, "Context switches", 16) == 0 ||
        kstrncmp(line, "Uptime:", 7) == 0) return 0xFFB8C7D8u;
    return CHARIS_UI_TEXT;
}

void terminal_render(void) {
    if (!term_visible || !g_framebuffer.initialized) return;

    u32 x, y, w, h, title_h;
    term_geometry(&x, &y, &w, &h, &title_h);

    /* Text and controls must never paint over the desktop or taskbar.
     * The framebuffer clip is reset before returning to the shell renderer. */
    fb_set_clip(x, y, w, h);

    u32 text_h = term_text_h();
    u32 line_h = text_h + term_px(10);
    u32 prompt_h = text_h + term_px(22);
    u32 content_top = y + title_h + term_px(44);
    u32 content_bottom = y + h - prompt_h - term_px(38);
    u32 rows = content_bottom > content_top ? (content_bottom - content_top) / line_h : 1;
    if (!rows) rows = 1;

    /* Window frame and application chrome. */
    fb_fill_rect(x + term_px(5), y + term_px(6), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(x, y, w, title_h, CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x, y + title_h, w, term_px(1), CHARIS_UI_BORDER);

    /* Three restrained window controls and a real session title. */
    u32 dot_y = y + title_h / 2;
    fb_fill_rect(x + term_px(16), dot_y - term_px(4), term_px(8), term_px(8), CHARIS_UI_DANGER);
    fb_fill_rect(x + term_px(32), dot_y - term_px(4), term_px(8), term_px(8), CHARIS_UI_WARNING);
    fb_fill_rect(x + term_px(48), dot_y - term_px(4), term_px(8), term_px(8), CHARIS_UI_SUCCESS);
    term_draw(x + term_px(72), y + (title_h - text_h) / 2,
                    "Terminal", CHARIS_UI_TEXT, FB_TRANSPARENT);
    const char* session_label = "LOCAL SESSION";
    u32 session_width = term_measure(session_label);
    u32 session_right = x + w > term_px(48) ? x + w - term_px(48) : x + w;
    u32 session_x = session_right > session_width + term_px(16)
                  ? session_right - session_width - term_px(16) : x + term_px(72);
    term_draw(session_x, y + (title_h - text_h) / 2,
              session_label, CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 close_x = x + w - term_px(34);
    u32 close_y = y + (title_h - term_px(24)) / 2u;
    fb_fill_rect(close_x, close_y, term_px(26), term_px(24), CHARIS_UI_SURFACE);
    fb_draw_rect(close_x, close_y, term_px(26), term_px(24), CHARIS_UI_BORDER);
    term_draw(close_x + term_px(9), close_y + (term_px(24) - text_h) / 2u,
              "x", CHARIS_UI_TEXT, FB_TRANSPARENT);

    /* Session strip separates app identity from command output. */
    u32 strip_y = y + title_h + term_px(1);
    fb_fill_rect(x, strip_y, w, term_px(32), CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x + term_px(16), strip_y + term_px(8), term_px(3), term_px(16),
                 CHARIS_UI_ACCENT);
    term_draw(x + term_px(28), strip_y + (term_px(32) - text_h) / 2,
                    "charisos  /  shell", CHARIS_UI_MUTED, FB_TRANSPARENT);
    const char* platform_label = "UTF-8  |  x86_64";
    u32 platform_width = term_measure(platform_label);
    u32 platform_x = x + w > platform_width + term_px(16)
                   ? x + w - platform_width - term_px(16) : x + term_px(28);
    term_draw(platform_x, strip_y + (term_px(32) - text_h) / 2,
              platform_label, CHARIS_UI_MUTED, FB_TRANSPARENT);

    /* Console canvas with a line-number gutter. */
    u32 canvas_y = content_top - term_px(6);
    u32 canvas_h = content_bottom > canvas_y ? content_bottom - canvas_y : term_px(40);
    fb_fill_rect(x + term_px(10), canvas_y, w - term_px(20), canvas_h,
                 0xFF0B1016u);
    fb_fill_rect(x + term_px(54), canvas_y + term_px(8), term_px(1),
                 canvas_h > term_px(16) ? canvas_h - term_px(16) : canvas_h,
                 0xFF27313Du);

    u32 text_x = x + term_px(68);
    u32 text_right = x + w - term_px(24);
    u32 text_width = text_right > text_x ? text_right - text_x : 1;
    u32 total_rows = 0;
    for (u32 i = 0; i < term_line_count; i++)
        total_rows += term_wrapped_rows(term_lines[i], text_width);
    u32 skip_rows = total_rows > rows ? total_rows - rows : 0;
    fb_set_clip(text_x, canvas_y, text_width, canvas_h);
    u32 drawn_rows = 0;
    for (u32 i = 0; i < term_line_count; i++) {
        const char* line = term_lines[i];
        u32 pos = 0;
        do {
            u32 end = pos, next = pos;
            term_next_segment(line, pos, text_width, &end, &next);
            if (skip_rows) {
                skip_rows--;
            } else if (drawn_rows < rows) {
                u32 line_y = content_top + drawn_rows * line_h;
                if (pos == 0) {
                    char line_no[8];
                    term_number(line_no, i + 1);
                    term_draw(x + term_px(20), line_y, line_no,
                              0xFF667383u, FB_TRANSPARENT);
                }
                char segment[TERM_LINE_MAX];
                u32 count = end - pos;
                if (count >= sizeof(segment)) count = sizeof(segment) - 1;
                for (u32 j = 0; j < count; j++) segment[j] = line[pos + j];
                segment[count] = 0;
                term_draw(text_x, line_y, segment,
                          terminal_line_color(line), FB_TRANSPARENT);
                drawn_rows++;
            }
            if (next <= pos || !line[next]) break;
            pos = next;
        } while (drawn_rows < rows || skip_rows);
    }
    fb_set_clip(x, y, w, h);

    /* Prompt is a distinct input control, not another line in the transcript. */
    u32 prompt_y = y + h - prompt_h - term_px(38);
    fb_fill_rect(x + term_px(12), prompt_y, w - term_px(24),
                 prompt_h, CHARIS_UI_PANEL_RAISED);
    fb_draw_rect(x + term_px(12), prompt_y, w - term_px(24),
                 prompt_h, CHARIS_UI_BORDER);
    term_draw(x + term_px(24), prompt_y + (prompt_h - text_h) / 2,
                    "charisos", CHARIS_UI_SUCCESS, FB_TRANSPARENT);
    u32 prefix_w = 0;
    gfx_text_size("charisos", &prefix_w, NULL);
    prefix_w *= TERM_TEXT_SCALE;
    term_draw(x + term_px(24) + prefix_w, prompt_y + (prompt_h - text_h) / 2,
                    ">", CHARIS_UI_ACCENT, FB_TRANSPARENT);
    u32 prompt_x = x + term_px(24) + prefix_w + term_px(14);
    u32 prompt_right = x + w - term_px(28);
    u32 input_available = prompt_right > prompt_x
                        ? prompt_right - prompt_x : 1;
    u32 input_start = 0;
    u32 input_width = term_measure(term_input);
    while (input_width > input_available && term_input[input_start]) {
        input_start++;
        input_width = term_measure(term_input + input_start);
    }
    char visible_input[TERM_INPUT_MAX];
    kstrncpy(visible_input, term_input + input_start,
             sizeof(visible_input) - 1);
    visible_input[sizeof(visible_input) - 1] = 0;
    fb_set_clip(prompt_x, prompt_y, input_available, prompt_h);
    term_draw(prompt_x, prompt_y + (prompt_h - text_h) / 2,
              visible_input, CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_fill_rect(prompt_x + input_width + term_px(2),
                 prompt_y + (prompt_h - text_h) / 2,
                 term_px(2), text_h, CHARIS_UI_ACCENT);
    fb_set_clip(x, y, w, h);

    u32 status_y = y + h - term_px(25);
    term_draw(x + term_px(18), status_y,
                    "READY", CHARIS_UI_SUCCESS, FB_TRANSPARENT);
    const char* status_label = "Kernel command environment";
    u32 ready_width = term_measure("READY");
    u32 status_width = term_measure(status_label);
    u32 footer_need = term_px(18) + ready_width +
                      term_px(12) + status_width + term_px(18);
    /* Omit the secondary status rather than letting it collide with READY. */
    if (w > footer_need) {
        u32 status_x = x + w - status_width - term_px(18);
        term_draw(status_x, status_y, status_label,
                  CHARIS_UI_MUTED, FB_TRANSPARENT);
    }

    fb_reset_clip();
}
