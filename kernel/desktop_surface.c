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
    u32 accent = kind == 0 ? DS_BLUE : kind == 1 ? DS_AMBER : DS_GREEN;
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
    } else {
        fb_draw_rect(x + u(7), y + u(6), u(20), u(22), accent);
        fb_fill_rect(x + u(11), y + u(11), u(12), u(2), accent);
        fb_fill_rect(x + u(11), y + u(16), u(12), u(2), accent);
        fb_fill_rect(x + u(11), y + u(21), u(8), u(2), accent);
    }
}

static void draw_shortcuts(void) {
    static const char *names[] = { "Browser", "Files", "Terminal" };
    u32 y = u(28);
    for (u32 i = 0; i < 3; ++i) {
        draw_app_icon(u(22), y, i);
        label(u(16), y + u(40), names[i], DS_WHITE);
        y += u(82);
    }
}

static void draw_status_widget(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 w = u(276), h = u(190);
    if (sw < w + u(24) || sh < h + u(40)) return;
    u32 x = sw - w - u(22);
    u32 y = u(22);
    fb_fill_rect(x + u(4), y + u(4), w, h, 0x88060B12u);
    fb_fill_rect(x, y, w, h, DS_PANEL_SOLID);
    fb_draw_rect(x, y, w, h, DS_LINE);
    fb_fill_rect(x, y, w, u(26), 0xFF202D3Eu);
    label(x + u(12), y + u(7), "SYSTEM OVERVIEW", DS_WHITE);
    fb_fill_rect(x + u(12), y + u(39), u(7), u(7), DS_GREEN);
    label(x + u(27), y + u(36), "Kernel running", DS_WHITE);

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
    append_text(memory, sizeof(memory), &pos, " / ");
    append_u32(memory, sizeof(memory), &pos, (stats.memory_used + stats.memory_free) / 1024);
    append_text(memory, sizeof(memory), &pos, " MB");
    pos = 0; tasks[0] = 0;
    append_text(tasks, sizeof(tasks), &pos, "Tasks ");
    append_u32(tasks, sizeof(tasks), &pos, stats.processes_active);
    append_text(tasks, sizeof(tasks), &pos, " / ");
    append_u32(tasks, sizeof(tasks), &pos, stats.processes_total);

    label(x + u(12), y + u(62), "Uptime", DS_MUTED);
    label(x + u(116), y + u(62), time, DS_WHITE);
    label(x + u(12), y + u(92), memory, DS_WHITE);
    label(x + u(12), y + u(122), tasks, DS_WHITE);
    label(x + u(12), y + u(154), "Managed physical memory", DS_MUTED);
    fb_fill_rect(x + u(12), y + u(176), w - u(24), u(2), DS_LINE);
    fb_fill_rect(x + u(12), y + u(176),
                 (w - u(24)) / 3, u(2), DS_BLUE);
}

static void draw_notification_widget(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 w = u(276), h = u(82);
    u32 y = u(228);
    if (sw < w + u(24) || sh < y + h + u(80)) return;
    u32 x = sw - w - u(22);
    fb_fill_rect(x + u(4), y + u(4), w, h, 0x88060B12u);
    fb_fill_rect(x, y, w, h, DS_PANEL_SOLID);
    fb_draw_rect(x, y, w, h, DS_LINE);
    label(x + u(12), y + u(10), "NOTIFICATIONS", DS_WHITE);
    fb_fill_rect(x + u(12), y + u(34), u(3), u(34), DS_BLUE);
    label(x + u(24), y + u(34), "CharisOS is ready", DS_WHITE);
    label(x + u(24), y + u(54), "Your live session is active.", DS_MUTED);
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    gfx_draw_string_scaled(u(82), u(34), "CHARIS OS", DS_WHITE, FB_TRANSPARENT, 2);
    label(u(84), u(70), "A native desktop environment", DS_MUTED);
    draw_shortcuts();
    draw_status_widget();
    draw_notification_widget();
}

/* Return the shortcut under the pointer: 0 browser, 1 files, 2 terminal. */
int desktop_surface_hit_test(int mx, int my) {
    u32 left = u(12), right = u(66), top = u(28), stride = u(82);
    if (mx < (int)left || mx >= (int)right || my < (int)top ||
        my >= (int)(top + stride * 3)) return -1;
    u32 relative = (u32)(my - (int)top);
    u32 slot = relative / stride;
    if (slot >= 3 || relative % stride >= u(68)) return -1;
    return (int)slot;
}

/* Three compact pinned launchers beside the Start button. */
void desktop_surface_draw_taskbar_icons(u32 y) {
    draw_app_icon(u(116), y + u(5), 0);
    draw_app_icon(u(160), y + u(5), 1);
    draw_app_icon(u(204), y + u(5), 2);
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(112) && mx < (int)u(154)) return 0;
    if (mx >= (int)u(156) && mx < (int)u(198)) return 1;
    if (mx >= (int)u(200) && mx < (int)u(242)) return 2;
    return -1;
}
