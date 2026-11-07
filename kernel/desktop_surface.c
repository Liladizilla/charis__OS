/* desktop_surface.c - CharisOS workspace composition.
 *
 * A useful desktop should communicate system state and provide a clear home,
 * not leave a lone shortcut floating on an empty framebuffer.
 */
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/ui_theme.h>
#include <kernel/diagnostics.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <kernel/string.h>

static u32 u(u32 value) { return gfx_ui_scale(value); }
static u32 text_h(void) { return (u32)font_cell_h * gfx_text_get_scale(); }

static void number(char* out, u32 value) {
    char rev[10]; u32 n = 0, p = 0;
    do { rev[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value && n < sizeof(rev));
    while (n) out[p++] = rev[--n];
    out[p] = 0;
}

static void metric_card(u32 x, u32 y, u32 w, const char* label,
                        const char* value, const char* detail, u32 accent) {
    u32 h = u(104), th = text_h();
    fb_fill_rect(x + u(2), y + u(3), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER);
    fb_fill_rect(x, y, u(3), h, accent);
    gfx_draw_string(x + u(16), y + u(12), label, CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + u(16), y + u(38), value, CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + u(16), y + h - th - u(10), detail, CHARIS_UI_MUTED, FB_TRANSPARENT);
}

static void draw_app_icon(u32 x, u32 y, u32 kind, u32 size) {
    u32 plate = CHARIS_UI_SURFACE, ink = CHARIS_UI_TEXT;
    if (kind == 0) { plate = 0xFF283B50u; ink = CHARIS_UI_ACCENT; }
    if (kind == 1) { plate = 0xFF3A3021u; ink = 0xFFF0C674u; }
    if (kind == 2) { plate = 0xFF20372Fu; ink = 0xFF83D6B0u; }
    if (kind == 3) { plate = 0xFF29313Bu; ink = CHARIS_UI_TEXT; }
    fb_fill_rect(x + u(2), y + u(2), size, size, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, size, size, plate);
    fb_draw_rect(x, y, size, size, CHARIS_UI_BORDER);
    u32 pad = size / 5, inner = size - pad * 2;
    if (kind == 0) {
        fb_draw_rect(x + pad, y + pad, inner, inner, ink);
        fb_fill_rect(x + pad, y + size / 2, inner, u(1), ink);
        fb_fill_rect(x + size / 2, y + pad, u(1), inner, ink);
    } else if (kind == 1) {
        fb_fill_rect(x + pad, y + pad + u(2), inner / 2, u(4), ink);
        fb_fill_rect(x + pad, y + pad + u(5), inner, inner - u(4), ink);
    } else if (kind == 2) {
        fb_draw_rect(x + pad, y + pad, inner, inner, ink);
        gfx_draw_string(x + pad + u(3), y + size / 2 - text_h() / 2,
                        ">_", ink, FB_TRANSPARENT);
    } else {
        fb_draw_rect(x + pad, y + pad, inner, inner, ink);
        fb_fill_rect(x + size / 2 - u(2), y + pad, u(4), inner, ink);
        fb_fill_rect(x + pad, y + size / 2 - u(2), inner, u(4), ink);
    }
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 margin = u(36), left = margin, top = u(34);
    u32 available = sw > margin * 2 ? sw - margin * 2 : sw;
    system_stats_t stats;
    diag_collect_stats(&stats);

    gfx_draw_string(left, top, "CHARISOS  /  WORKSPACE",
                    CHARIS_UI_ACCENT, FB_TRANSPARENT);
    gfx_draw_string(left, top + u(28), "A focused environment for your work.",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(left, top + u(52), "SYSTEM OVERVIEW",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    u32 gap = u(14), card_w = available > gap * 2 ? (available - gap * 2) / 3 : available / 3;
    if (card_w < u(120)) card_w = u(120);
    u32 card_y = top + u(78);
    char value[16], detail[32];

    u32 uptime = (u32)(timer_get_ms() / 60000u);
    number(value, uptime);
    kstrncpy(detail, "minutes since boot", sizeof(detail) - 1);
    metric_card(left, card_y, card_w, "UPTIME", value, detail, CHARIS_UI_ACCENT);

    number(value, pmm_free_pages() / 256u);
    kstrncpy(detail, "MiB available", sizeof(detail) - 1);
    metric_card(left + card_w + gap, card_y, card_w, "FREE MEMORY",
                value, detail, CHARIS_UI_SUCCESS);

    number(value, stats.processes_active);
    kstrncpy(detail, "active kernel tasks", sizeof(detail) - 1);
    metric_card(left + (card_w + gap) * 2, card_y, card_w, "TASKS",
                value, detail, CHARIS_UI_WARNING);

    u32 section_y = card_y + u(132);
    gfx_draw_string(left, section_y, "QUICK ACCESS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    /* The Files shortcut remains the only desktop icon with a wired hit-test.
     * Other applications are available from the taskbar and Start menu. */
    u32 icon = u(48), x = left, y = section_y + u(28);
    draw_app_icon(x, y, 1, icon);
    gfx_draw_string(x, y + icon + u(10), "Files", CHARIS_UI_TEXT, FB_TRANSPARENT);

    u32 note_y = y + icon + u(48);
    if (sh > note_y + u(30))
        gfx_draw_string(left, note_y, "Your system at a glance. Applications are pinned below.",
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
}

int desktop_surface_hit_test(int mx, int my) {
    u32 x = u(36), y = u(34) + u(78) + u(132) + u(28), icon = u(48);
    u32 label_h = text_h(), width = u(64);
    u32 left = x > (width - icon) / 2 ? x - (width - icon) / 2 : 0;
    if (mx < (int)left || mx >= (int)(left + width)) return -1;
    if (my < (int)y || my >= (int)(y + icon + u(10) + label_h)) return -1;
    return 1;
}

void desktop_surface_draw_taskbar_icons(u32 y) {
    u32 size = u(28), center = u(44) > size ? (u(44) - size) / 2 : 0;
    draw_app_icon(u(116), y + center, 0, size);
    draw_app_icon(u(160), y + center, 1, size);
    draw_app_icon(u(204), y + center, 2, size);
    draw_app_icon(u(248), y + center, 3, size);
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(112) && mx < (int)u(154)) return 0;
    if (mx >= (int)u(156) && mx < (int)u(198)) return 1;
    if (mx >= (int)u(200) && mx < (int)u(242)) return 2;
    if (mx >= (int)u(244) && mx < (int)u(286)) return 3;
    return -1;
}
