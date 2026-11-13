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
#include <kernel/config.h>

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
        /* Draw the prompt mark with framebuffer strokes, not a font glyph:
         * icons must keep fixed geometry when the user enlarges text. */
        u32 stroke = u(2u);
        u32 leg = inner / 4u;
        u32 cx = x + p + inner / 4u;
        u32 cy = y + p + inner / 2u;
        for (u32 i = 0; i <= leg; i++) {
            fb_fill_rect(cx + i, cy - i, stroke, stroke, ink);
            fb_fill_rect(cx + i, cy + i, stroke, stroke, ink);
        }
        fb_fill_rect(cx + leg + u(3u), cy + leg + u(1u),
                     inner / 3u, stroke, ink);
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

static u32 draw_u32(u32 x, u32 y, u32 value, u32 color) {
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
    u32 width = 0;
    gfx_text_size(out, &width, NULL);
    return width;
}

static u32 draw_u32_suffix(u32 x, u32 y, u32 value,
                           const char* suffix, u32 color) {
    u32 width = draw_u32(x, y, value, color);
    u32 suffix_width = 0;
    gfx_text_size(suffix, &suffix_width, NULL);
    gfx_draw_string(x + width + u(6u), y, suffix, color, FB_TRANSPARENT);
    return width + u(6u) + suffix_width;
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

static u32 draw_system_panel(void) {
    if (!system_panel_fits()) return 0;
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

    return y + panel_h;
}

/* The lower workspace widget presents live allocator and display-profile
 * data. It is intentionally skipped at the largest font size if the content
 * cannot fit above the taskbar; desktop widgets must never overlap controls. */
static u32 shortcut_grid_bottom(void) {
    u32 bottom = 0;
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++) {
        u32 x = 0, y = 0;
        shortcut_position(i, &x, &y);
        u32 edge = y + u(shortcut_card_height());
        if (edge > bottom) bottom = edge;
    }
    return bottom;
}

static u32 draw_workspace_status(u32 content_bottom) {
    if (gfx_text_get_scale() > 2u || g_framebuffer.width <= u(96u))
        return content_bottom;

    u32 panel_x = u(48u);
    u32 panel_w = g_framebuffer.width - u(96u);
    if (panel_w < u(320u)) return content_bottom;
    u32 panel_y = content_bottom + u(28u);
    u32 line = th();

    u32 header_y = panel_y + u(14u);
    u32 divider_y = header_y + line + u(8u);
    u32 label_y = divider_y + u(14u);
    u32 value_y = label_y + line + u(4u);
    u32 footer_y = value_y + line + u(4u);
    u32 bar_y = footer_y + line + u(8u);
    u32 bar_h = u(10u);
    u32 panel_h = bar_y - panel_y + bar_h + u(14u);
    u32 bottom_limit = g_framebuffer.height > u(56u)
                     ? g_framebuffer.height - u(56u) : 0;
    if (panel_y >= bottom_limit || panel_h > bottom_limit - panel_y)
        return content_bottom;

    u32 left = panel_x + u(20u);
    u32 right = panel_x + panel_w / 2u + u(12u);
    u32 col_w = panel_w / 2u - u(36u);

    fb_fill_rect(panel_x, panel_y, panel_w, panel_h, CHARIS_UI_PANEL);
    fb_draw_rect(panel_x, panel_y, panel_w, panel_h, CHARIS_UI_BORDER);
    fb_fill_rect(panel_x, panel_y, panel_w, u(3u), CHARIS_UI_ACCENT);
    gfx_draw_string(left, header_y, "SYSTEM STATUS",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_fill_rect(left, divider_y, panel_w - u(40u), u(1u),
                 CHARIS_UI_BORDER);

    fb_set_clip(left, label_y, col_w, line);
    gfx_draw_string(left, label_y, "MEMORY ALLOCATION",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();
    fb_set_clip(right, label_y, col_w, line);
    gfx_draw_string(right, label_y, "DISPLAY PROFILE",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_reset_clip();

    u32 used_mib = pmm_used_pages() / 256u;
    u32 total_mib = pmm_total_pages() / 256u;
    u32 free_mib = pmm_free_pages() / 256u;

    fb_set_clip(left, value_y, col_w, line);
    draw_u32_suffix(left, value_y, used_mib, " MiB allocated",
                    CHARIS_UI_TEXT);
    fb_reset_clip();

    fb_set_clip(left, footer_y, col_w, line);
    draw_u32_suffix(left, footer_y, free_mib, " MiB free",
                    CHARIS_UI_MUTED);
    fb_reset_clip();

    u64 total_pages = pmm_total_pages();
    u64 used_pages = pmm_used_pages();
    u32 fill_w = total_pages
               ? (u32)((used_pages * (u64)col_w) / total_pages) : 0;
    if (used_pages && !fill_w) fill_w = u(2u);
    if (fill_w > col_w) fill_w = col_w;
    fb_fill_rect(left, bar_y, col_w, bar_h, CHARIS_UI_SURFACE);
    if (fill_w) fb_fill_rect(left, bar_y, fill_w, bar_h, CHARIS_UI_ACCENT);
    fb_draw_rect(left, bar_y, col_w, bar_h, CHARIS_UI_BORDER);

    fb_set_clip(right, value_y, col_w, line);
    u32 cursor = draw_u32(right, value_y, g_framebuffer.width,
                          CHARIS_UI_TEXT);
    gfx_draw_string(right + cursor + u(6u), value_y, "x",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 x_width = 0;
    gfx_text_size("x", &x_width, NULL);
    cursor += u(6u) + x_width;
    draw_u32(right + cursor + u(6u), value_y, g_framebuffer.height,
             CHARIS_UI_TEXT);
    fb_reset_clip();

    int font_scale = config_get_int("font_scale", 1);
    if (font_scale < 1 || font_scale > 3) font_scale = 1;
    int ui_scale = config_get_int("ui_scale", 100);
    if (ui_scale < 80 || ui_scale > 120) ui_scale = 100;
    fb_set_clip(right, footer_y, col_w, line);
    u32 scale_x = right;
    gfx_draw_string(scale_x, footer_y, "FONT", CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 prefix_w = 0;
    gfx_text_size("FONT ", &prefix_w, NULL);
    scale_x += prefix_w;
    scale_x += draw_u32(scale_x, footer_y, (u32)font_scale * 100u,
                        CHARIS_UI_TEXT);
    gfx_draw_string(scale_x, footer_y, "%  UI", CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 middle_w = 0;
    gfx_text_size("%  UI ", &middle_w, NULL);
    scale_x += middle_w;
    scale_x += draw_u32(scale_x, footer_y, (u32)ui_scale,
                        CHARIS_UI_TEXT);
    gfx_draw_string(scale_x, footer_y, "%", CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_reset_clip();

    (void)total_mib;
    return panel_y + panel_h;
}

/* Compact live diagnostics. Read the counters directly: diag_collect_stats()
 * walks the scheduler list and is intentionally not used in this repaint path. */
static void draw_workspace_activity(u32 content_bottom) {
    if (gfx_text_get_scale() > 2u || g_framebuffer.width <= u(96u)) return;

    u32 panel_x = u(48u);
    u32 panel_w = g_framebuffer.width - u(96u);
    if (panel_w < u(480u)) return;

    u32 panel_y = content_bottom + u(20u);
    u32 line = th();
    u32 header_y = panel_y + u(14u);
    u32 divider_y = header_y + line + u(8u);
    u32 label_y = divider_y + u(14u);
    u32 value_y = label_y + line + u(5u);
    u32 panel_h = value_y - panel_y + line + u(12u);
    u32 bottom_limit = g_framebuffer.height > u(56u)
                     ? g_framebuffer.height - u(56u) : 0;
    if (panel_y >= bottom_limit || panel_h > bottom_limit - panel_y) return;

    u32 inner_width = panel_w - u(40u);
    u32 gap = u(24u);
    u32 col_w = (inner_width - gap * 2u) / 3u;
    if (col_w < u(96u)) return;

    u32 context_switches = 0, interrupts = 0, syscalls = 0;
    diag_get_runtime_counters(&context_switches, &interrupts, &syscalls);

    fb_fill_rect(panel_x, panel_y, panel_w, panel_h, CHARIS_UI_PANEL);
    fb_draw_rect(panel_x, panel_y, panel_w, panel_h, CHARIS_UI_BORDER);
    fb_fill_rect(panel_x, panel_y, panel_w, u(3u), CHARIS_UI_ACCENT);
    gfx_draw_string(panel_x + u(20u), header_y, "KERNEL ACTIVITY",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_fill_rect(panel_x + u(20u), divider_y, panel_w - u(40u), u(1u),
                 CHARIS_UI_BORDER);

    static const char* const compact_labels[3] = {
        "SWITCHES", "IRQS", "SYSCALLS"
    };
    static const char* const full_labels[3] = {
        "CONTEXT SWITCHES", "INTERRUPTS", "SYSCALLS"
    };
    const char* const* labels = gfx_text_get_scale() >= 2u
                              ? compact_labels : full_labels;
    const u32 values[3] = { context_switches, interrupts, syscalls };
    for (u32 i = 0; i < 3u; i++) {
        u32 x = panel_x + u(20u) + i * (col_w + gap);
        fb_set_clip(x, label_y, col_w, line);
        gfx_draw_string(x, label_y, labels[i],
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
        fb_reset_clip();

        fb_set_clip(x, value_y, col_w, line);
        draw_u32(x, value_y, values[i], CHARIS_UI_ACCENT);
        fb_reset_clip();
    }
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    u32 width = g_framebuffer.width;
    u32 size = 44u;
    u32 heading_x = u(48u), heading_y = u(40u);
    u32 heading_w = 0, heading_h = 0;
    gfx_text_size("WORKSPACE", &heading_w, &heading_h);

    /* Keep the workspace title legible over photographs and bright wallpaper.
     * This is an opaque graphite title strip, not text painted directly over
     * arbitrary image pixels. */
    u32 title_left = u(32u);
    u32 title_width = width > u(64u) ? width-u(64u) : width;
    u32 title_top = heading_y > u(6u) ? heading_y-u(6u) : 0u;
    u32 title_height = heading_h + u(12u);
    fb_fill_rect(title_left, title_top, title_width, title_height,
                 CHARIS_UI_PANEL);
    fb_draw_rect(title_left, title_top, title_width, title_height,
                 CHARIS_UI_BORDER);
    fb_fill_rect(title_left, title_top, title_width, u(2u),
                 CHARIS_UI_ACCENT);
    gfx_draw_string(heading_x, heading_y, "WORKSPACE",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    u32 secondary_x = heading_x + heading_w + u(24u);
    u32 secondary_w = 0;
    gfx_text_size("CORE APPLICATIONS", &secondary_w, NULL);
    if (secondary_x <= width && width - secondary_x > u(48u) &&
        secondary_w <= width - secondary_x - u(48u))
        gfx_draw_string(secondary_x, heading_y, "CORE APPLICATIONS",
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
    u32 divider_y = heading_y + heading_h + u(16u);
    fb_fill_rect(u(48), divider_y, width > u(96) ? width-u(96) : width,
                 u(1), CHARIS_UI_BORDER);
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++)
        draw_shortcut(i, size);

    u32 content_bottom = shortcut_grid_bottom();
    u32 system_bottom = draw_system_panel();
    if (system_bottom > content_bottom) content_bottom = system_bottom;
    content_bottom = draw_workspace_status(content_bottom);
    draw_workspace_activity(content_bottom);
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
