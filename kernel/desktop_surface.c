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

static void app_icon(u32 x, u32 y, u32 kind, u32 size) {
    u32 ink = kind == 0 ? 0xFF86B9E8u :
              kind == 1 ? 0xFFF0C674u :
              kind == 2 ? 0xFF83D6B0u :
              kind == 4 ? 0xFF8FC7D8u : CHARIS_UI_ACCENT;
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
    } else if (kind == 3) {
        fb_draw_rect(x+p+u(3), y+p+u(3), inner-u(6), inner-u(6), ink);
        fb_fill_rect(x+size/2u-u(2), y+p, u(4), inner, ink);
        fb_fill_rect(x+p, y+size/2u-u(2), inner, u(4), ink);
    } else {
        /* Browser: a compact globe/window mark with meridian and equator. */
        fb_draw_rect(x+p, y+p, inner, inner, ink);
        fb_fill_rect(x+p, y+size/2u-u(1), inner, u(2), ink);
        fb_fill_rect(x+size/2u-u(1), y+p, u(2), inner, ink);
        fb_fill_rect(x+p+u(3), y+p+u(5), inner-u(6), u(1), ink);
    }
}

/* Workspace geometry is expressed in logical pixels and scaled at draw time.
 * Four wide launch surfaces create a real application grid; a compact system
 * panel uses only O(1) counters so desktop repaint never walks task lists. */
#define DESKTOP_SHORTCUT_COUNT 4u
#define DESKTOP_CELL_W 184u
#define DESKTOP_CARD_GAP_X 16u
#define DESKTOP_CARD_GAP_Y 16u
#define DESKTOP_GRID_X 48u
#define DESKTOP_GRID_Y 112u
static const u32 shortcut_icon[DESKTOP_SHORTCUT_COUNT] = { 1u, 2u, 3u, 4u };
static const char* const shortcut_label[DESKTOP_SHORTCUT_COUNT] = {
    "Files", "Terminal", "Settings", "Browser"
};
static const char* const shortcut_detail[DESKTOP_SHORTCUT_COUNT] = {
    "Browse local volumes", "Run commands and tools",
    "Personalize your system", "Explore local pages"
};

static void draw_u32(u32 x, u32 y, u32 value, u32 color) {
    char digits[11];
    u32 count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value && count < sizeof(digits));
    char out[11];
    for (u32 i = 0; i < count; i++) out[i] = digits[count - i - 1u];
    out[count] = '\0';
    gfx_draw_string(x, y, out, color, FB_TRANSPARENT);
}

static void draw_uptime(u32 x, u32 y, u64 milliseconds) {
    u64 seconds = milliseconds / 1000ULL;
    u32 hours = (u32)(seconds / 3600ULL);
    u32 minutes = (u32)((seconds / 60ULL) % 60ULL);
    u32 secs = (u32)(seconds % 60ULL);
    char value[16];
    value[0] = hours > 99u ? '9' : (char)('0' + hours / 10u);
    value[1] = hours > 99u ? '9' : (char)('0' + hours % 10u);
    value[2] = ':';
    value[3] = (char)('0' + minutes / 10u);
    value[4] = (char)('0' + minutes % 10u);
    value[5] = ':';
    value[6] = (char)('0' + secs / 10u);
    value[7] = (char)('0' + secs % 10u);
    value[8] = '\0';
    gfx_draw_string(x, y, value, CHARIS_UI_TEXT, FB_TRANSPARENT);
}

/* Card height follows the selected font metrics. At the largest bitmap
 * size the detail line is intentionally omitted: preserving readable labels
 * is more useful than squeezing two colliding lines into a fixed card. */
static bool shortcut_show_detail(void) {
    return gfx_text_get_scale() < 3u && g_framebuffer.height >= u(600u);
}

static u32 shortcut_card_height(void) {
    u32 line = th();
    return shortcut_show_detail() ? line * 2u + 24u : line + 16u;
}

/* Keep the two-column composition when there is room. Larger bitmap
 * fonts get wider cards; compact framebuffers switch to one column instead
 * of drawing labels underneath the system panel or beyond the right edge. */
static u32 shortcut_columns(void) {
    u32 width = g_framebuffer.width;
    u32 panel_reserved = (width >= u(900) && g_framebuffer.height >= u(480))
                       ? u(360u + 24u) : 0u;
    u32 left = u(DESKTOP_GRID_X);
    u32 right = width > panel_reserved + u(48u)
              ? width - panel_reserved - u(48u) : 0u;
    u32 available = right > left ? right - left : 0u;
    u32 font_scale = gfx_text_get_scale();
    u32 desired = u(DESKTOP_CELL_W + (font_scale > 1u ? (font_scale - 1u) * 48u : 0u));
    u32 gap = u(DESKTOP_CARD_GAP_X);
    return available >= desired * 2u + gap ? 2u : 1u;
}

static u32 shortcut_card_width(void) {
    u32 width = g_framebuffer.width;
    u32 panel_reserved = (width >= u(900) && g_framebuffer.height >= u(480))
                       ? u(360u + 24u) : 0u;
    u32 left = u(DESKTOP_GRID_X);
    u32 right = width > panel_reserved + u(48u)
              ? width - panel_reserved - u(48u) : 0u;
    u32 available = right > left ? right - left : 1u;
    if (shortcut_columns() == 2u) {
        u32 font_scale = gfx_text_get_scale();
        u32 desired = u(DESKTOP_CELL_W + (font_scale > 1u ? (font_scale - 1u) * 48u : 0u));
        return desired;
    }
    return available;
}

static u32 desktop_grid_top(void) {
    u32 heading_height = 0;
    gfx_text_size("WORKSPACE", 0, &heading_height);
    u32 top = u(40u) + heading_height + u(32u);
    return top > u(DESKTOP_GRID_Y) ? top : u(DESKTOP_GRID_Y);
}

static void shortcut_position(u32 index, u32* x, u32* y) {
    u32 columns = shortcut_columns();
    u32 column = index % columns;
    u32 row = index / columns;
    u32 card_w = shortcut_card_width();
    u32 card_h = shortcut_card_height();
    if (x) *x = u(DESKTOP_GRID_X) + column * (card_w + u(DESKTOP_CARD_GAP_X));
    if (y) *y = desktop_grid_top() + row * (card_h + u(DESKTOP_CARD_GAP_Y));
}

static void draw_shortcut(u32 index, u32 icon_size) {
    u32 logical_x = 0, logical_y = 0;
    shortcut_position(index, &logical_x, &logical_y);
    u32 x = logical_x, y = logical_y;
    u32 w = shortcut_card_width(), h = u(shortcut_card_height());
    u32 icon = u(icon_size);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER);
    fb_fill_rect(x, y, u(3), h, CHARIS_UI_ACCENT);
    app_icon(x + u(16), y + (h-icon)/2u, shortcut_icon[index], icon);

    u32 text_x = x + u(72);
    u32 text_h = th();
    if (w < u(112)) text_x = x + u(16);
    u32 label_y;
    if (!shortcut_show_detail()) {
        label_y = y + (h > text_h ? (h - text_h) / 2u : 0u);
    } else {
        u32 block_h = text_h * 2u + u(4);
        label_y = y + (h > block_h ? (h - block_h) / 2u : 0u);
    }
    fb_set_clip(text_x, y + u(4), w > u(80) ? w - u(84) : 1u,
                h > u(8) ? h - u(8) : 1u);
    gfx_draw_string(text_x, label_y, shortcut_label[index],
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    if (shortcut_show_detail()) {
        fb_set_clip(x + u(16), label_y + text_h + u(4),
                    w > u(32) ? w - u(32) : 1u, text_h);
        gfx_draw_string(x + u(16), label_y + text_h + u(4),
                        shortcut_detail[index], CHARIS_UI_MUTED,
                        FB_TRANSPARENT);
        fb_reset_clip();
    }
    fb_reset_clip();
}

static void draw_system_panel(void) {
    u32 width = g_framebuffer.width;
    u32 height = g_framebuffer.height;
    if (width < u(900) || height < u(480)) return;
    u32 x = width - u(360), y = u(112), w = u(312), h = u(240);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER);
    fb_fill_rect(x, y, w, u(3), CHARIS_UI_ACCENT);
    gfx_draw_string(x + u(20), y + u(22), "SYSTEM OVERVIEW",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_fill_rect(x + u(20), y + u(48), w - u(40), u(1), CHARIS_UI_BORDER);
    gfx_draw_string(x + u(20), y + u(68), "UPTIME",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    draw_uptime(x + u(20), y + u(88), timer_get_ms());
    gfx_draw_string(x + u(164), y + u(68), "FREE RAM (MiB)",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    draw_u32(x + u(164), y + u(88), pmm_free_pages() / 256u,
             CHARIS_UI_TEXT);
    gfx_draw_string(x + u(20), y + u(132), "MANAGED (MiB)",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    draw_u32(x + u(20), y + u(152), pmm_total_pages() / 256u,
             CHARIS_UI_TEXT);
    gfx_draw_string(x + u(164), y + u(132), "ALLOCATED (MiB)",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    draw_u32(x + u(164), y + u(152), pmm_used_pages() / 256u,
             CHARIS_UI_TEXT);
    fb_fill_rect(x + u(20), y + u(188), w - u(40), u(1),
                 CHARIS_UI_BORDER);
    gfx_draw_string(x + u(20), y + u(204), "LIVE KERNEL COUNTERS",
                    CHARIS_UI_ACCENT, FB_TRANSPARENT);
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    u32 width = g_framebuffer.width;
    u32 size = 44u;
    u32 heading_x = u(48u), heading_y = u(40u);
    u32 heading_w = 0, heading_h = 0;
    gfx_text_size("WORKSPACE", &heading_w, &heading_h);
    gfx_draw_string(heading_x, heading_y, "WORKSPACE",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(heading_x + heading_w + u(24u), heading_y,
                    "CORE APPLICATIONS", CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 divider_y = heading_y + heading_h + u(16u);
    fb_fill_rect(u(48), divider_y, width > u(96) ? width-u(96) : width,
                 u(1), CHARIS_UI_BORDER);
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++)
        draw_shortcut(i, size);
    draw_system_panel();
}

int desktop_surface_hit_test(int mx, int my) {
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++) {
        u32 x = 0, y = 0;
        shortcut_position(i, &x, &y);
        u32 w = u(DESKTOP_CELL_W), h = u(shortcut_card_height());
        if (mx >= (int)x && mx < (int)(x+w) &&
            my >= (int)y && my < (int)(y+h)) return (int)i;
    }
    return -1;
}

void desktop_surface_draw_taskbar_icons(u32 y) {
    u32 size = u(28), center = u(44)>size ? (u(44)-size)/2u : 0;
    app_icon(u(112), y+center, 1, size); /* Files */
    app_icon(u(156), y+center, 2, size); /* Terminal */
    app_icon(u(200), y+center, 3, size); /* Settings */
    app_icon(u(244), y+center, 4, size); /* Browser */
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(108) && mx < (int)u(150)) return 1;
    if (mx >= (int)u(152) && mx < (int)u(194)) return 2;
    if (mx >= (int)u(196) && mx < (int)u(238)) return 3;
    if (mx >= (int)u(240) && mx < (int)u(282)) return 4;
    return -1;
}
