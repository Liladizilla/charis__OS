/* desktop_surface.c - Native CharisOS desktop shortcuts and live widgets.
 *
 * All geometry uses the global interface scale. The status surface reports
 * real uptime, managed physical memory and scheduler task counts only.
 */
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/timer.h>
#include <kernel/diagnostics.h>
#include <kernel/ui_theme.h>

#define DS_PANEL       0xE916202Cu
#define DS_PANEL_SOLID 0xFF161F2Cu
#define DS_LINE        0xFF35465Au
#define DS_MUTED       0xFF9AA9BCu
#define DS_WHITE       0xFFEAF1FAu
#define DS_BLUE        0xFF75BFFFu
#define DS_GREEN       0xFF83D6B0u
#define DS_AMBER       0xFFF0C674u

static u32 u(u32 value) { return gfx_ui_scale(value); }
static u32 text_h(void) { return (u32)font_cell_h * gfx_text_get_scale(); }
static u32 max_u32(u32 a, u32 b) { return a > b ? a : b; }
static u32 shortcut_stride(void) {
    return max_u32(u(82), u(40) + text_h() + u(12));
}

static void label(u32 x, u32 y, const char *s, u32 color) {
    gfx_draw_string(x, y, s, color, FB_TRANSPARENT);
}

static void append_u32(char* out, u32 capacity, u32* pos, u32 value) {
    char digits[11];
    u32 n = 0;
    do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < sizeof(digits));
    while (n && *pos + 1 < capacity) out[(*pos)++] = digits[--n];
    out[*pos] = 0;
}

static void append_text(char* out, u32 capacity, u32* pos, const char* value) {
    while (value && *value && *pos + 1 < capacity) out[(*pos)++] = *value++;
    out[*pos] = 0;
}

static void draw_app_icon(u32 x, u32 y, u32 kind) {
    u32 accent = kind == 0 ? DS_BLUE : kind == 1 ? DS_AMBER :
                 kind == 2 ? DS_GREEN : DS_AMBER;
    fb_fill_rect(x + u(2), y + u(3), u(34), u(34), 0xFF090F18u);
    fb_fill_rect(x, y, u(34), u(34), 0xFF202D3Eu);
    fb_draw_rect(x, y, u(34), u(34), DS_LINE);
    if (kind == 0) {
        fb_draw_rect(x + u(6), y + u(7), u(22), u(17), accent);
        fb_fill_rect(x + u(6), y + u(24), u(22), u(2), accent);
        fb_fill_rect(x + u(15), y + u(26), u(4), u(3), accent);
        fb_fill_rect(x + u(11), y + u(29), u(12), u(2), accent);
    } else if (kind == 1) {
        fb_fill_rect(x + u(6), y + u(8), u(22), u(18), accent);
        fb_fill_rect(x + u(6), y + u(8), u(22), u(4), 0xFFFFE7B3u);
        fb_fill_rect(x + u(10), y + u(15), u(14), u(2), 0xFF6B5120u);
        fb_fill_rect(x + u(10), y + u(20), u(10), u(2), 0xFF6B5120u);
    } else if (kind == 2) {
        fb_draw_rect(x + u(7), y + u(6), u(20), u(22), accent);
        fb_fill_rect(x + u(11), y + u(11), u(12), u(2), accent);
        fb_fill_rect(x + u(11), y + u(16), u(12), u(2), accent);
        fb_fill_rect(x + u(11), y + u(21), u(8), u(2), accent);
    } else {
        /* Compact settings gear: solid teeth and a hollow centre. */
        fb_fill_rect(x + u(14), y + u(5), u(6), u(5), accent);
        fb_fill_rect(x + u(14), y + u(24), u(6), u(5), accent);
        fb_fill_rect(x + u(5), y + u(14), u(5), u(6), accent);
        fb_fill_rect(x + u(24), y + u(14), u(5), u(6), accent);
        fb_fill_rect(x + u(8), y + u(8), u(5), u(5), accent);
        fb_fill_rect(x + u(21), y + u(8), u(5), u(5), accent);
        fb_fill_rect(x + u(8), y + u(21), u(5), u(5), accent);
        fb_fill_rect(x + u(21), y + u(21), u(5), u(5), accent);
        fb_draw_rect(x + u(11), y + u(11), u(12), u(12), accent);
        fb_fill_rect(x + u(15), y + u(15), u(4), u(4), 0xFF202D3Eu);
    }
}

static void draw_shortcuts(void) {
    static const char *names[] = { "Browser", "Files", "Terminal" };
    u32 y = u(28);
    u32 stride = shortcut_stride();
    for (u32 i = 0; i < 3; ++i) {
        draw_app_icon(u(22), y, i);
        label(u(16), y + u(40), names[i], DS_WHITE);
        y += stride;
    }
}

static void draw_status_widget(u32* out_x, u32* out_y, u32* out_w, u32* out_h) {
    u32 sw = g_framebuffer.width;
    u32 w = u(360);
    if (w > sw - (sw > u(24) ? u(24) : 0)) w = sw > u(24) ? sw - u(24) : sw;
    u32 gap = max_u32(u(30), text_h() + u(8));
    u32 title_h = max_u32(u(26), text_h() + u(12));
    u32 first_line = title_h + u(12);
    u32 ram_line = first_line + gap;
    u32 task_line = ram_line + gap;
    u32 activity_line = task_line + gap;
    u32 foot_line = activity_line + gap;
    u32 bar_y = foot_line + text_h() + u(10);
    u32 h = bar_y + u(14);
    u32 x = sw > w + u(22) ? sw - w - u(22) : 0;
    u32 y = u(22);

    fb_fill_rect(x + u(4), y + u(4), w, h, 0x88060B12u);
    fb_fill_rect(x, y, w, h, DS_PANEL_SOLID);
    fb_draw_rect(x, y, w, h, DS_LINE);
    fb_fill_rect(x, y, w, title_h, 0xFF202D3Eu);
    label(x + u(12), y + (title_h > text_h() ? (title_h - text_h()) / 2 : 0),
          "SYSTEM OVERVIEW", DS_WHITE);
    fb_fill_rect(x + u(12), y + first_line, u(7), u(7), DS_GREEN);

    u64 sec = timer_get_ms() / 1000;
    char time[24], digits[16];
    u64 hours = sec / 3600;
    u32 count = 0, pos = 0;
    u32 mm = (u32)((sec / 60) % 60), ss = (u32)(sec % 60);
    do { digits[count++] = (char)('0' + hours % 10); hours /= 10; }
    while (hours && count < sizeof(digits));
    while (count) time[pos++] = digits[--count];
    time[pos++] = ':';
    time[pos++] = (char)('0' + mm / 10); time[pos++] = (char)('0' + mm % 10);
    time[pos++] = ':';
    time[pos++] = (char)('0' + ss / 10); time[pos++] = (char)('0' + ss % 10);
    time[pos] = 0;

    system_stats_t stats;
    diag_collect_stats(&stats);
    char memory[48], tasks[48];
    pos = 0; memory[0] = 0;
    append_text(memory, sizeof(memory), &pos, "RAM ");
    append_u32(memory, sizeof(memory), &pos, stats.memory_used / 1024);
    append_text(memory, sizeof(memory), &pos, "/");
    append_u32(memory, sizeof(memory), &pos,
               (stats.memory_used + stats.memory_free) / 1024);
    append_text(memory, sizeof(memory), &pos, " MB");
    pos = 0; tasks[0] = 0;
    append_text(tasks, sizeof(tasks), &pos, "Tasks ");
    append_u32(tasks, sizeof(tasks), &pos, stats.processes_active);
    append_text(tasks, sizeof(tasks), &pos, "/");
    append_u32(tasks, sizeof(tasks), &pos, stats.processes_total);
    append_text(tasks, sizeof(tasks), &pos, "  B:");
    append_u32(tasks, sizeof(tasks), &pos, stats.processes_blocked);

    char activity[64];
    pos = 0; activity[0] = 0;
    append_text(activity, sizeof(activity), &pos, "CS ");
    append_u32(activity, sizeof(activity), &pos, stats.context_switches);
    append_text(activity, sizeof(activity), &pos, "  IRQ ");
    append_u32(activity, sizeof(activity), &pos, stats.interrupts);
    append_text(activity, sizeof(activity), &pos, "  SYS ");
    append_u32(activity, sizeof(activity), &pos, stats.syscalls);

    label(x + u(12), y + first_line, "Up", DS_MUTED);
    label(x + u(52), y + first_line, time, DS_WHITE);
    label(x + u(12), y + ram_line, memory, DS_WHITE);
    label(x + u(12), y + task_line, tasks, DS_WHITE);
    label(x + u(12), y + activity_line, activity, DS_MUTED);
    label(x + u(12), y + foot_line, "Managed physical memory", DS_MUTED);

    u32 bar_x = x + u(12), bar_w = w > u(24) ? w - u(24) : 1;
    fb_fill_rect(bar_x, y + bar_y, bar_w, u(2), DS_LINE);
    u64 total = (u64)stats.memory_used + stats.memory_free;
    u32 used_w = total ? (u32)(((u64)bar_w * stats.memory_used) / total) : 0;
    if (used_w > bar_w) used_w = bar_w;
    if (used_w) fb_fill_rect(bar_x, y + bar_y, used_w, u(2), DS_BLUE);

    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

static void draw_notification_widget(u32 x, u32 y, u32 w) {
    u32 title_h = max_u32(u(28), text_h() + u(10));
    u32 body_gap = text_h() + u(8);
    u32 h = title_h + body_gap * 2 + u(12);
    fb_fill_rect(x + u(4), y + u(4), w, h, 0x88060B12u);
    fb_fill_rect(x, y, w, h, DS_PANEL_SOLID);
    fb_draw_rect(x, y, w, h, DS_LINE);
    label(x + u(12), y + (title_h > text_h() ? (title_h - text_h()) / 2 : 0),
          "NOTIFICATIONS", DS_WHITE);
    fb_fill_rect(x + u(12), y + title_h + u(6), u(3), body_gap * 2 - u(8), DS_BLUE);
    label(x + u(24), y + title_h + u(4), "CharisOS is ready", DS_WHITE);
    label(x + u(24), y + title_h + body_gap, "Your live session is active.", DS_MUTED);
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    gfx_draw_string_scaled(u(82), u(34), "CHARIS OS", DS_WHITE, FB_TRANSPARENT, 2);
    label(u(84), u(70), "A native desktop environment", DS_MUTED);
    draw_shortcuts();
    u32 widget_x = 0, widget_y = 0, widget_w = 0, widget_h = 0;
    draw_status_widget(&widget_x, &widget_y, &widget_w, &widget_h);
    u32 notification_y = widget_y + widget_h + u(14);
    if (notification_y + text_h() + u(20) < g_framebuffer.height)
        draw_notification_widget(widget_x, notification_y, widget_w);
}

/* Return the shortcut under the pointer: 0 browser, 1 files, 2 terminal. */
int desktop_surface_hit_test(int mx, int my) {
    u32 left = u(12), right = u(66), top = u(28), stride = shortcut_stride();
    if (mx < (int)left || mx >= (int)right || my < (int)top ||
        my >= (int)(top + stride * 3)) return -1;
    u32 relative = (u32)(my - (int)top);
    u32 slot = relative / stride;
    u32 clickable_height = u(40) + text_h();
    if (slot >= 3 || relative % stride >= clickable_height) return -1;
    return (int)slot;
}

/* Three compact pinned launchers beside the Start button. */
void desktop_surface_draw_taskbar_icons(u32 y) {
    draw_app_icon(u(116), y + u(5), 0);
    draw_app_icon(u(160), y + u(5), 1);
    draw_app_icon(u(204), y + u(5), 2);
    draw_app_icon(u(248), y + u(5), 3);
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(112) && mx < (int)u(154)) return 0;
    if (mx >= (int)u(156) && mx < (int)u(198)) return 1;
    if (mx >= (int)u(200) && mx < (int)u(242)) return 2;
    if (mx >= (int)u(244) && mx < (int)u(286)) return 3;
    return -1;
}
