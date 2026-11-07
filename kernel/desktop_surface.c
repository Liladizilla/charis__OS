/* desktop_surface.c - CharisOS desktop workspace.
 * The framebuffer desktop uses a restrained graphite/amber visual system:
 * clear hierarchy, useful system status, and application launch surfaces.
 */
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/ui_theme.h>
#include <kernel/diagnostics.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <kernel/string.h>

static u32 u(u32 v) { return gfx_ui_scale(v); }
static u32 th(void) { return (u32)font_cell_h * gfx_text_get_scale(); }

static void number(char* out, u32 value) {
    char rev[10]; u32 n = 0, p = 0;
    do { rev[n++] = (char)('0' + value % 10u); value /= 10u; }
    while (value && n < sizeof(rev));
    while (n) out[p++] = rev[--n];
    out[p] = 0;
}

static void panel(u32 x, u32 y, u32 w, u32 h, u32 fill) {
    fb_fill_rect(x + u(3), y + u(4), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, fill);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER);
}

static void app_icon(u32 x, u32 y, u32 kind, u32 size) {
    u32 ink = kind == 0 ? 0xFF86B9E8u :
              kind == 1 ? 0xFFF0C674u :
              kind == 2 ? 0xFF83D6B0u : 0xFFB7A0F5u;
    fb_fill_rect(x, y, size, size, CHARIS_UI_SURFACE);
    fb_draw_rect(x, y, size, size, CHARIS_UI_BORDER_STRONG);
    u32 p = size / 5u, inner = size - p * 2u;
    if (kind == 0) {
        fb_draw_rect(x+p, y+p, inner, inner, ink);
        fb_fill_rect(x+p, y+size/2u, inner, u(1), ink);
        fb_fill_rect(x+size/2u, y+p, u(1), inner, ink);
    } else if (kind == 1) {
        fb_fill_rect(x+p, y+p+u(2), inner/2u, u(5), ink);
        fb_fill_rect(x+p, y+p+u(6), inner, inner-u(5), ink);
    } else if (kind == 2) {
        fb_draw_rect(x+p, y+p, inner, inner, ink);
        gfx_draw_string_scaled(x+p+u(3), y+size/2u-th(), ">_", ink,
                               CHARIS_UI_SURFACE, gfx_text_get_scale());
    } else {
        fb_draw_rect(x+p+u(3), y+p+u(3), inner-u(6), inner-u(6), ink);
        fb_fill_rect(x+size/2u-u(2), y+p, u(4), inner, ink);
        fb_fill_rect(x+p, y+size/2u-u(2), inner, u(4), ink);
    }
}

static void app_tile(u32 x, u32 y, u32 w, u32 h, u32 kind,
                     const char* title, const char* detail) {
    panel(x, y, w, h, CHARIS_UI_PANEL);
    app_icon(x+u(16), y+(h-u(42))/2u, kind, u(42));
    gfx_draw_string(x+u(72), y+u(26), title, CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x+u(72), y+u(52), detail, CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_fill_rect(x+w-u(4), y+u(12), u(2), h-u(24),
                 kind == 0 ? 0xFF86B9E8u :
                 kind == 1 ? 0xFFF0C674u :
                 kind == 2 ? 0xFF83D6B0u : 0xFFB7A0F5u);
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 m = u(36), gap = u(16);
    u32 usable = sw > m*2u ? sw-m*2u : sw;
    u32 top = u(28), hero_h = u(184);
    u32 left_w = usable > u(380) ? usable-u(380)-gap : usable*2u/3u;
    u32 right_x = m+left_w+gap;
    u32 right_w = usable > left_w+gap ? usable-left_w-gap : 0;

    /* Header mark and product identity. */
    fb_fill_rect(m, top+u(2), u(4), u(20), CHARIS_UI_ACCENT);
    gfx_draw_string(m+u(14), top, "CHARISOS  /  DESKTOP",
                    CHARIS_UI_ACCENT, FB_TRANSPARENT);
    gfx_draw_string(m, top+u(32), "Your workspace.",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(m, top+u(58), "A focused environment built around your work.",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    /* Primary welcome surface. */
    u32 hero_y = top+u(84);
    panel(m, hero_y, left_w, hero_h, CHARIS_UI_PANEL);
    fb_fill_rect(m, hero_y, u(4), hero_h, CHARIS_UI_ACCENT);
    gfx_draw_string(m+u(22), hero_y+u(20), "WELCOME BACK",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string_scaled(m+u(22), hero_y+u(54), "CharisOS",
                           CHARIS_UI_TEXT, FB_TRANSPARENT, 2);
    gfx_draw_string(m+u(22), hero_y+u(100),
                    "A native x86_64 desktop environment.",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(m+u(22), hero_y+u(130),
                    "Open an application to get started.",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_fill_rect(m+left_w-u(112), hero_y+u(24), u(84), u(84),
                 CHARIS_UI_ACCENT_SURFACE);
    fb_draw_rect(m+left_w-u(112), hero_y+u(24), u(84), u(84),
                 CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(m+left_w-u(92), hero_y+u(44), u(44), u(44),
                 CHARIS_UI_ACCENT);
    fb_fill_rect(m+left_w-u(82), hero_y+u(54), u(24), u(24),
                 CHARIS_UI_PANEL);

    /* Live system card. */
    panel(right_x, hero_y, right_w, hero_h, CHARIS_UI_PANEL);
    gfx_draw_string(right_x+u(18), hero_y+u(18), "SYSTEM STATUS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    system_stats_t stats;
    diag_collect_stats(&stats);
    char value[16];
    number(value, pmm_free_pages()/256u);
    gfx_draw_string(right_x+u(18), hero_y+u(48), value,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(right_x+u(68), hero_y+u(48), "MiB FREE RAM",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_fill_rect(right_x+u(18), hero_y+u(76), right_w-u(36), u(1),
                 CHARIS_UI_BORDER);
    number(value, stats.processes_active);
    gfx_draw_string(right_x+u(18), hero_y+u(92), value,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(right_x+u(68), hero_y+u(92), "ACTIVE TASKS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    number(value, (u32)(timer_get_ms()/60000u));
    gfx_draw_string(right_x+u(18), hero_y+u(136), value,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(right_x+u(68), hero_y+u(136), "MINUTES UPTIME",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    /* Consistent application tiles with real launch hit targets. */
    u32 section_y = hero_y+hero_h+u(28);
    gfx_draw_string(m, section_y, "APPLICATIONS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 tile_y = section_y+u(28), tile_h = u(88);
    u32 tile_gap = u(14), tile_w = usable > tile_gap*3u
        ? (usable-tile_gap*3u)/4u : usable/4u;
    static const char* const names[4] = {
        "Browser", "Files", "Terminal", "Settings"
    };
    static const char* const desc[4] = {
        "Web workspace", "Browse volumes", "Command line", "System controls"
    };
    for (u32 i=0; i<4; i++)
        app_tile(m+i*(tile_w+tile_gap), tile_y, tile_w, tile_h,
                 i, names[i], desc[i]);

    /* A restrained footer keeps the canvas intentional without fake widgets. */
    u32 footer_y = tile_y+tile_h+u(28);
    if (sh > footer_y+u(18)) {
        fb_fill_rect(m, footer_y, usable, u(1), CHARIS_UI_BORDER);
        gfx_draw_string(m, footer_y+u(12),
                        "CHARISOS  |  LOCAL SESSION  |  X86_64",
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
    }
}

int desktop_surface_hit_test(int mx, int my) {
    u32 sw = g_framebuffer.width, m = u(36), gap = u(14);
    u32 usable = sw > m*2u ? sw-m*2u : sw;
    u32 left_w = usable > u(380) ? usable-u(380)-u(16) : usable*2u/3u;
    u32 hero_y = u(28)+u(84);
    u32 tile_y = hero_y+u(184)+u(28)+u(28);
    u32 tile_h = u(88), tile_gap = gap;
    u32 tile_w = usable > tile_gap*3u
        ? (usable-tile_gap*3u)/4u : usable/4u;
    (void)left_w;
    if (my < (int)tile_y || my >= (int)(tile_y+tile_h)) return -1;
    for (u32 i=0; i<4; i++) {
        u32 x = m+i*(tile_w+tile_gap);
        if (mx >= (int)x && mx < (int)(x+tile_w)) return (int)i;
    }
    return -1;
}

void desktop_surface_draw_taskbar_icons(u32 y) {
    u32 size = u(28), center = u(44)>size ? (u(44)-size)/2u : 0;
    app_icon(u(116), y+center, 0, size);
    app_icon(u(160), y+center, 1, size);
    app_icon(u(204), y+center, 2, size);
    app_icon(u(248), y+center, 3, size);
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(112) && mx < (int)u(154)) return 0;
    if (mx >= (int)u(156) && mx < (int)u(198)) return 1;
    if (mx >= (int)u(200) && mx < (int)u(242)) return 2;
    if (mx >= (int)u(244) && mx < (int)u(286)) return 3;
    return -1;
}
