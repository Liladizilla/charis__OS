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

typedef struct {
    u32 margin, usable, top, hero_y, hero_h;
    u32 left_w, right_x, right_w;
    u32 section_y, tile_y, tile_h, tile_gap, tile_w;
} desktop_layout_t;

static desktop_layout_t desktop_layout(void) {
    desktop_layout_t d;
    u32 sw = g_framebuffer.width;
    u32 text_h = th();
    d.margin = u(36);
    d.tile_gap = u(14);
    d.usable = sw > d.margin * 2u ? sw - d.margin * 2u : sw;
    d.top = u(28);
    d.hero_y = d.top + u(84);
    d.left_w = d.usable > u(380) ? d.usable - u(380) - u(16)
                                 : d.usable * 2u / 3u;
    d.right_x = d.margin + d.left_w + u(16);
    d.right_w = d.usable > d.left_w + u(16)
              ? d.usable - d.left_w - u(16) : 0;

    u32 brand_y = u(20) + text_h + u(12);
    u32 line_one_y = brand_y + text_h * 2u + u(10);
    u32 line_two_y = line_one_y + text_h + u(8);
    u32 left_need = line_two_y + text_h + u(18);
    u32 right_need = u(18) + text_h + u(18) + u(1) +
                     u(14) + text_h + u(18) + text_h + u(18);
    d.hero_h = u(184);
    if (d.hero_h < left_need) d.hero_h = left_need;
    if (d.hero_h < right_need) d.hero_h = right_need;

    d.section_y = d.hero_y + d.hero_h + u(28);
    d.tile_y = d.section_y + u(28);
    u32 tile_need = u(16) + text_h + u(8) + text_h + u(12);
    d.tile_h = u(88) > tile_need ? u(88) : tile_need;
    d.tile_w = d.usable > d.tile_gap * 3u
             ? (d.usable - d.tile_gap * 3u) / 4u : d.usable / 4u;
    return d;
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
    desktop_layout_t d = desktop_layout();
    u32 text_h = th();

    fb_fill_rect(d.margin, d.top+u(2), u(4), u(20), CHARIS_UI_ACCENT);
    gfx_draw_string(d.margin+u(14), d.top, "CHARISOS  /  DESKTOP",
                    CHARIS_UI_ACCENT, FB_TRANSPARENT);
    gfx_draw_string(d.margin, d.top+u(32), "Your workspace.",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(d.margin, d.top+u(58), "A focused environment built around your work.",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    panel(d.margin, d.hero_y, d.left_w, d.hero_h, CHARIS_UI_PANEL);
    fb_fill_rect(d.margin, d.hero_y, u(4), d.hero_h, CHARIS_UI_ACCENT);
    gfx_draw_string(d.margin+u(22), d.hero_y+u(20), "WELCOME BACK",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 brand_y = d.hero_y + u(20) + text_h + u(12);
    gfx_draw_string_scaled(d.margin+u(22), brand_y, "CharisOS",
                           CHARIS_UI_TEXT, FB_TRANSPARENT, 2);
    u32 line_one_y = brand_y + text_h * 2u + u(10);
    u32 line_two_y = line_one_y + text_h + u(8);
    u32 text_right = d.margin + d.left_w - u(126);
    u32 text_x = d.margin + u(22);
    if (text_right > text_x) fb_set_clip(text_x, d.hero_y, text_right-text_x, d.hero_h);
    gfx_draw_string(text_x, line_one_y,
                    "A native x86_64 desktop environment.",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(text_x, line_two_y,
                    "Open an application to get started.",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();

    fb_fill_rect(d.margin+d.left_w-u(112), d.hero_y+u(24), u(84), u(84),
                 CHARIS_UI_ACCENT_SURFACE);
    fb_draw_rect(d.margin+d.left_w-u(112), d.hero_y+u(24), u(84), u(84),
                 CHARIS_UI_BORDER_STRONG);
    fb_fill_rect(d.margin+d.left_w-u(92), d.hero_y+u(44), u(44), u(44),
                 CHARIS_UI_ACCENT);
    fb_fill_rect(d.margin+d.left_w-u(82), d.hero_y+u(54), u(24), u(24),
                 CHARIS_UI_PANEL);

    panel(d.right_x, d.hero_y, d.right_w, d.hero_h, CHARIS_UI_PANEL);
    gfx_draw_string(d.right_x+u(18), d.hero_y+u(18), "SYSTEM STATUS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    system_stats_t stats;
    diag_collect_stats(&stats);
    char value[16];
    number(value, pmm_free_pages()/256u);
    gfx_draw_string(d.right_x+u(18), d.hero_y+u(48), value,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(d.right_x+u(68), d.hero_y+u(48), "MiB FREE RAM",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 divider_y = d.hero_y+u(76);
    fb_fill_rect(d.right_x+u(18), divider_y,
                 d.right_w > u(36) ? d.right_w-u(36) : 1, u(1),
                 CHARIS_UI_BORDER);
    u32 stat_two_y = divider_y+u(14)+text_h;
    number(value, stats.processes_active);
    gfx_draw_string(d.right_x+u(18), stat_two_y, value,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(d.right_x+u(68), stat_two_y, "ACTIVE TASKS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 stat_three_y = stat_two_y+text_h+u(18);
    number(value, (u32)(timer_get_ms()/60000u));
    gfx_draw_string(d.right_x+u(18), stat_three_y, value,
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(d.right_x+u(68), stat_three_y, "MINUTES UPTIME",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);

    gfx_draw_string(d.margin, d.section_y, "APPLICATIONS",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    static const char* const names[4] = {
        "Browser", "Files", "Terminal", "Settings"
    };
    static const char* const desc[4] = {
        "Web workspace", "Browse volumes", "Command line", "System controls"
    };
    for (u32 i=0; i<4; i++) {
        u32 tx = d.margin+i*(d.tile_w+d.tile_gap);
        panel(tx, d.tile_y, d.tile_w, d.tile_h, CHARIS_UI_PANEL);
        app_icon(tx+u(16), d.tile_y+(d.tile_h-u(42))/2u, i, u(42));
        u32 label_x = tx+u(72);
        u32 label_w = d.tile_w > u(88) ? d.tile_w-u(88) : 1;
        fb_set_clip(label_x, d.tile_y, label_w, d.tile_h);
        u32 title_y = d.tile_y+u(16);
        gfx_draw_string(label_x, title_y, names[i],
                        CHARIS_UI_TEXT, FB_TRANSPARENT);
        gfx_draw_string(label_x, title_y+text_h+u(8), desc[i],
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
        fb_reset_clip();
        fb_fill_rect(tx+d.tile_w-u(4), d.tile_y+u(12), u(2),
                     d.tile_h > u(24) ? d.tile_h-u(24) : 1,
                     i == 0 ? 0xFF86B9E8u :
                     i == 1 ? 0xFFF0C674u :
                     i == 2 ? 0xFF83D6B0u : 0xFFB7A0F5u);
    }

    u32 footer_y = d.tile_y+d.tile_h+u(28);
    if (g_framebuffer.height > footer_y+text_h+u(18)) {
        fb_fill_rect(d.margin, footer_y, d.usable, u(1), CHARIS_UI_BORDER);
        gfx_draw_string(d.margin, footer_y+u(12),
                        "CHARISOS  |  LOCAL SESSION  |  X86_64",
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
    }
}

int desktop_surface_hit_test(int mx, int my) {
    desktop_layout_t d = desktop_layout();
    if (my < (int)d.tile_y || my >= (int)(d.tile_y+d.tile_h)) return -1;
    for (u32 i=0; i<4; i++) {
        u32 x = d.margin+i*(d.tile_w+d.tile_gap);
        if (mx >= (int)x && mx < (int)(x+d.tile_w)) return (int)i;
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
