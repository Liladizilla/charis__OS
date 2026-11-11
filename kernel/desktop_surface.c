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
                               CHARIS_UI_SURFACE, 1u);
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
    "Local files", "Command line", "Preferences", "Web pages"
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

/* Descriptions are a secondary affordance, not worth clipping at larger
 * text sizes. Keep them only at the baseline font scale; the card itself
 * expands with the selected font metrics. */
static bool shortcut_show_detail(void) {
    return gfx_text_get_scale() == 1u && g_framebuffer.height >= u(600u);
}

static u32 shortcut_card_height(void) {
    u32 line = th();
    return shortcut_show_detail() ? line * 2u + 24u : line + 16u;
}

/* Reserve a right-hand status lane only when the framebuffer can contain it.
 * Wider font presets receive a wider panel so values and labels stay readable. */
static u32 system_panel_logical_width(void) {
    u32 scale = gfx_text_get_scale();
    return scale >= 3u ? 440u : (scale == 2u ? 400u : 312u);
}

static bool system_panel_fits(void) {
    u32 minimum_height = gfx_text_get_scale() >= 3u ? 520u : 480u;
    return g_framebuffer.width >= u(760u) &&
           g_framebuffer.height >= u(minimum_height);
}

/* The workspace uses two or four columns, avoiding an unbalanced
 * three-plus-one final row. Cards expand to fill the available lane instead
 * of leaving a narrow 184px strip stranded beside the system panel. */
static u32 shortcut_available_width(void) {
    u32 width = g_framebuffer.width;
    u32 panel_reserved = system_panel_fits()
                       ? u(system_panel_logical_width() + 24u) : 0u;
    u32 left = u(DESKTOP_GRID_X);
    u32 right = width > panel_reserved + u(48u)
              ? width - panel_reserved - u(48u) : 0u;
    return right > left ? right - left : 1u;
}

static u32 shortcut_minimum_width(void) {
    u32 font_scale = gfx_text_get_scale();
    u32 logical = DESKTOP_CELL_W +
                  (font_scale > 1u ? (font_scale - 1u) * 48u : 0u);
    return u(logical);
}

static u32 shortcut_columns(void) {
    u32 available = shortcut_available_width();
    u32 desired = shortcut_minimum_width();
    u32 gap = u(DESKTOP_CARD_GAP_X);
    if (available >= desired * 4u + gap * 3u) return 4u;
    return available >= desired * 2u + gap ? 2u : 1u;
}

static u32 shortcut_card_width(void) {
    u32 available = shortcut_available_width();
    u32 columns = shortcut_columns();
    if (columns == 1u) return available;
    u32 gaps = u(DESKTOP_CARD_GAP_X) * (columns - 1u);
    u32 card_width = available > gaps ? (available - gaps) / columns : 1u;
    u32 maximum = u(360u);
    return card_width > maximum ? maximum : card_width;
}

static u32 shortcut_grid_left(void) {
    u32 left = u(DESKTOP_GRID_X);
    u32 available = shortcut_available_width();
    u32 columns = shortcut_columns();
    u32 used = shortcut_card_width() * columns +
               u(DESKTOP_CARD_GAP_X) * (columns - 1u);
    return available > used ? left + (available - used) / 2u : left;
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
    u32 card_h = u(shortcut_card_height());
    if (x) *x = shortcut_grid_left() + column * (card_w + u(DESKTOP_CARD_GAP_X));
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
        /* Keep the secondary line in the same text column as the title.
         * Starting it at the card inset made it run underneath the icon. */
        u32 text_offset = text_x - x;
        u32 detail_width = w > text_offset + u(12)
                         ? w - text_offset - u(12) : 1u;
        u32 detail_y = label_y + text_h + u(4);
        fb_set_clip(text_x, detail_y, detail_width, text_h);
        gfx_draw_string(text_x, detail_y, shortcut_detail[index],
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
        fb_reset_clip();
    }
    fb_reset_clip();
}

static void draw_system_panel(void) {
    if (!system_panel_fits()) return;
    u32 width = g_framebuffer.width;
    u32 panel_w = u(system_panel_logical_width());
    u32 x = width - panel_w - u(48u);
    u32 y = desktop_grid_top();
    u32 line = th();
    u32 left = x + u(20u);
    u32 right = x + panel_w / 2u + u(4u);
    u32 col_w = panel_w / 2u - u(28u);

    /* Labels and values use separate baselines. The previous fixed 20px
     * separation collided as soon as a larger bitmap font was selected. */
    u32 header_y = y + u(18u);
    u32 divider_y = header_y + line + u(10u);
    u32 row1_label_y = divider_y + u(16u);
    u32 row1_value_y = row1_label_y + line + u(4u);
    u32 row2_label_y = row1_value_y + line + u(12u);
    u32 row2_value_y = row2_label_y + line + u(4u);
    u32 panel_h = row2_value_y - y + line + u(16u);

    fb_fill_rect(x, y, panel_w, panel_h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, panel_w, panel_h, CHARIS_UI_BORDER);
    fb_fill_rect(x, y, panel_w, u(3u), CHARIS_UI_ACCENT);
    gfx_draw_string(left, header_y, "SYSTEM OVERVIEW",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_fill_rect(left, divider_y, panel_w - u(40u), u(1u),
                 CHARIS_UI_BORDER);

    const char* free_label = gfx_text_get_scale() >= 2u ? "FREE MiB" : "FREE RAM (MiB)";
    const char* total_label = gfx_text_get_scale() >= 2u ? "TOTAL MiB" : "MANAGED (MiB)";
    const char* used_label = gfx_text_get_scale() >= 2u ? "USED MiB" : "ALLOCATED (MiB)";

    fb_set_clip(left, row1_label_y, col_w, line);
    gfx_draw_string(left, row1_label_y, "UPTIME",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();
    fb_set_clip(right, row1_label_y, col_w, line);
    gfx_draw_string(right, row1_label_y, free_label,
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();

    fb_set_clip(left, row1_value_y, col_w, line);
    draw_uptime(left, row1_value_y, timer_get_ms());
    fb_reset_clip();
    fb_set_clip(right, row1_value_y, col_w, line);
    draw_u32(right, row1_value_y, pmm_free_pages() / 256u, CHARIS_UI_TEXT);
    fb_reset_clip();

    fb_set_clip(left, row2_label_y, col_w, line);
    gfx_draw_string(left, row2_label_y, total_label,
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();
    fb_set_clip(right, row2_label_y, col_w, line);
    gfx_draw_string(right, row2_label_y, used_label,
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();

    fb_set_clip(left, row2_value_y, col_w, line);
    draw_u32(left, row2_value_y, pmm_total_pages() / 256u, CHARIS_UI_TEXT);
    fb_reset_clip();
    fb_set_clip(right, row2_value_y, col_w, line);
    draw_u32(right, row2_value_y, pmm_used_pages() / 256u, CHARIS_UI_TEXT);
    fb_reset_clip();
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
        u32 w = shortcut_card_width(), h = u(shortcut_card_height());
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
