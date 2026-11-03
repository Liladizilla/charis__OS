/* desktop_surface.c - Desktop shortcuts and at-a-glance widgets.
 *
 * This is deliberately a native framebuffer surface: no browser, DOM, script
 * runtime or external assets are involved. All drawing is clipped by fb.c.
 */
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/timer.h>
#include <kernel/ui_theme.h>

#define DS_PANEL       0xE916202Cu
#define DS_PANEL_SOLID 0xFF161F2Cu
#define DS_LINE        0xFF35465Au
#define DS_MUTED       0xFF9AA9BCu
#define DS_WHITE       0xFFEAF1FAu
#define DS_BLUE        0xFF75BFFFu
#define DS_GREEN       0xFF83D6B0u
#define DS_AMBER       0xFFF0C674u

static void label(u32 x, u32 y, const char *s, u32 color) {
    gfx_draw_string(x, y, s, color, FB_TRANSPARENT);
}

static void draw_app_icon(u32 x, u32 y, u32 kind) {
    u32 accent = kind == 0 ? DS_BLUE : kind == 1 ? DS_AMBER : DS_GREEN;
    fb_fill_rect(x + 2, y + 3, 34, 34, 0xFF090F18u);
    fb_fill_rect(x, y, 34, 34, 0xFF202D3Eu);
    fb_draw_rect(x, y, 34, 34, DS_LINE);
    if (kind == 0) {
        fb_draw_rect(x + 6, y + 7, 22, 17, accent);
        fb_fill_rect(x + 6, y + 24, 22, 2, accent);
        fb_fill_rect(x + 15, y + 26, 4, 3, accent);
        fb_fill_rect(x + 11, y + 29, 12, 2, accent);
    } else if (kind == 1) {
        fb_fill_rect(x + 6, y + 8, 22, 18, accent);
        fb_fill_rect(x + 6, y + 8, 22, 4, 0xFFFFE7B3u);
        fb_fill_rect(x + 10, y + 15, 14, 2, 0xFF6B5120u);
        fb_fill_rect(x + 10, y + 20, 10, 2, 0xFF6B5120u);
    } else {
        fb_draw_rect(x + 7, y + 6, 20, 22, accent);
        fb_fill_rect(x + 11, y + 11, 12, 2, accent);
        fb_fill_rect(x + 11, y + 16, 12, 2, accent);
        fb_fill_rect(x + 11, y + 21, 8, 2, accent);
    }
}

static void draw_shortcuts(void) {
    static const char *names[] = { "Browser", "Files", "Terminal" };
    u32 y = 28;
    for (u32 i = 0; i < 3; ++i) {
        draw_app_icon(22, y, i);
        label(16, y + 40, names[i], DS_WHITE);
        y += 82;
    }
}

static void draw_status_widget(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    if (sw < 520 || sh < 250) return;
    u32 w = sw > 1180 ? 260 : 220;
    u32 x = sw - w - 22;
    u32 y = 22;
    fb_fill_rect(x + 4, y + 4, w, 126, 0x88060B12u);
    fb_fill_rect(x, y, w, 126, DS_PANEL_SOLID);
    fb_draw_rect(x, y, w, 126, DS_LINE);
    fb_fill_rect(x, y, w, 26, 0xFF202D3Eu);
    label(x + 12, y + 7, "SYSTEM OVERVIEW", DS_WHITE);
    fb_fill_rect(x + 12, y + 39, 7, 7, DS_GREEN);
    label(x + 27, y + 36, "Kernel running", DS_WHITE);
    label(x + 12, y + 59, "Session", DS_MUTED);
    label(x + 90, y + 59, "Live environment", DS_WHITE);
    label(x + 12, y + 82, "Uptime", DS_MUTED);
    u64 sec = timer_get_ms() / 1000;
    char time[16];
    u32 hh = (u32)(sec / 3600), mm = (u32)((sec / 60) % 60), ss = (u32)(sec % 60);
    time[0] = (char)('0' + (hh / 10) % 10); time[1] = (char)('0' + hh % 10);
    time[2] = ':'; time[3] = (char)('0' + mm / 10); time[4] = (char)('0' + mm % 10);
    time[5] = ':'; time[6] = (char)('0' + ss / 10); time[7] = (char)('0' + ss % 10);
    time[8] = 0;
    label(x + 90, y + 82, time, DS_WHITE);
    fb_fill_rect(x + 12, y + 108, w - 24, 2, DS_LINE);
    fb_fill_rect(x + 12, y + 108, (w - 24) / 3, 2, DS_BLUE);
}

static void draw_notification_widget(void) {
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    if (sw < 700 || sh < 340) return;
    u32 w = sw > 1180 ? 260 : 220;
    u32 x = sw - w - 22;
    u32 y = 166;
    fb_fill_rect(x + 4, y + 4, w, 88, 0x88060B12u);
    fb_fill_rect(x, y, w, 88, DS_PANEL_SOLID);
    fb_draw_rect(x, y, w, 88, DS_LINE);
    label(x + 12, y + 10, "NOTIFICATIONS", DS_WHITE);
    fb_fill_rect(x + 12, y + 34, 3, 34, DS_BLUE);
    label(x + 24, y + 34, "CharisOS is ready", DS_WHITE);
    label(x + 24, y + 54, "Your live session is active.", DS_MUTED);
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    draw_shortcuts();
    draw_status_widget();
    draw_notification_widget();
}

/* Return the shortcut under the pointer: 0 browser, 1 files, 2 terminal. */
int desktop_surface_hit_test(int mx, int my) {
    if (mx < 12 || mx >= 66 || my < 28 || my >= 274) return -1;
    u32 slot = (u32)(my - 28) / 82;
    if (slot >= 3) return -1;
    if ((u32)(my - 28) % 82 >= 68) return -1;
    return (int)slot;
}
