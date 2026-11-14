/* desktop_surface.c - Wallpaper-first CharisOS desktop.
 *
 * The wallpaper is the primary desktop surface. Launchers are a compact
 * left-edge icon column; kernel telemetry belongs in System Monitor, not
 * permanently over the user's workspace.
 */
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/input.h>
#include <kernel/ui_theme.h>

static u32 desktop_px(u32 value) { return gfx_ui_scale(value); }
static u32 desktop_text_height(void) {
    return (u32)font_cell_h * gfx_text_get_scale();
}

#define DESKTOP_SHORTCUT_COUNT 4u
#define DESKTOP_CELL_WIDTH 80u
#define DESKTOP_CELL_HEIGHT 90u
#define DESKTOP_ICON_SIZE 40u
#define DESKTOP_GRID_X 18u
#define DESKTOP_GRID_Y 22u

static const u32 shortcut_icon[DESKTOP_SHORTCUT_COUNT] = { 1u, 2u, 3u, 4u };
static const char* const shortcut_label[DESKTOP_SHORTCUT_COUNT] = {
    "Files", "Terminal", "Settings", "Browser"
};

static void app_icon(u32 x, u32 y, u32 kind, u32 size) {
    u32 ink = kind == 1u ? 0xFFF0C674u :
              kind == 2u ? 0xFF83D6B0u :
              kind == 4u ? 0xFF8FC7D8u : CHARIS_UI_ACCENT;
    fb_fill_rect(x, y, size, size, CHARIS_UI_SURFACE);
    fb_draw_rect(x, y, size, size, CHARIS_UI_BORDER_STRONG);
    u32 p = size / 5u, inner = size - p * 2u;
    if (kind == 1u) {
        fb_fill_rect(x + p, y + p + desktop_px(2u), inner / 2u,
                     desktop_px(5u), ink);
        fb_fill_rect(x + p, y + p + desktop_px(6u), inner,
                     inner > desktop_px(5u) ? inner - desktop_px(5u) : 1u, ink);
    } else if (kind == 2u) {
        fb_draw_rect(x + p, y + p, inner, inner, ink);
        u32 stroke = desktop_px(2u), leg = inner / 4u;
        u32 cx = x + p + inner / 4u, cy = y + p + inner / 2u;
        for (u32 i = 0; i <= leg; i++) {
            fb_fill_rect(cx + i, cy - i, stroke, stroke, ink);
            fb_fill_rect(cx + i, cy + i, stroke, stroke, ink);
        }
        fb_fill_rect(cx + leg + desktop_px(3u), cy + leg + desktop_px(1u),
                     inner / 3u, stroke, ink);
    } else if (kind == 3u) {
        fb_draw_rect(x + p + desktop_px(2u), y + p + desktop_px(2u),
                     inner > desktop_px(4u) ? inner - desktop_px(4u) : 1u,
                     inner > desktop_px(4u) ? inner - desktop_px(4u) : 1u, ink);
        fb_fill_rect(x + size / 2u - desktop_px(2u), y + p,
                     desktop_px(4u), inner, ink);
        fb_fill_rect(x + p, y + size / 2u - desktop_px(2u),
                     inner, desktop_px(4u), ink);
    } else {
        fb_draw_rect(x + p, y + p, inner, inner, ink);
        fb_fill_rect(x + p, y + size / 2u - desktop_px(1u),
                     inner, desktop_px(2u), ink);
        fb_fill_rect(x + size / 2u - desktop_px(1u), y + p,
                     desktop_px(2u), inner, ink);
    }
}

static u32 shortcut_cell_width(void) {
    u32 width = 0;
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++) {
        u32 current = 0;
        gfx_text_size(shortcut_label[i], &current, NULL);
        if (current > width) width = current;
    }
    u32 minimum = desktop_px(DESKTOP_CELL_WIDTH);
    u32 needed = width + desktop_px(12u);
    return needed > minimum ? needed : minimum;
}

static u32 shortcut_cell_height(void) {
    u32 minimum = desktop_px(DESKTOP_CELL_HEIGHT);
    u32 needed = desktop_px(DESKTOP_ICON_SIZE) +
                 desktop_text_height() + desktop_px(16u);
    return needed > minimum ? needed : minimum;
}

static void shortcut_position(u32 index, u32* x, u32* y) {
    if (x) *x = desktop_px(DESKTOP_GRID_X);
    if (y) *y = desktop_px(DESKTOP_GRID_Y) + index * shortcut_cell_height();
}

static void draw_shortcut(u32 index) {
    u32 x = 0, y = 0;
    shortcut_position(index, &x, &y);
    u32 width = shortcut_cell_width();
    u32 height = shortcut_cell_height();
    int mx = input_get_mouse_x(), my = input_get_mouse_y();
    bool hovered = mx >= (int)x && mx < (int)(x + width) &&
                   my >= (int)y && my < (int)(y + height);
    if (hovered) {
        fb_fill_rect(x, y, width, height, CHARIS_UI_ACCENT_SURFACE);
        fb_draw_rect(x, y, width, height, CHARIS_UI_BORDER_STRONG);
    }

    u32 icon_size = desktop_px(DESKTOP_ICON_SIZE);
    u32 icon_x = x + (width > icon_size ? (width - icon_size) / 2u : 0u);
    u32 icon_y = y + desktop_px(4u);
    app_icon(icon_x, icon_y, shortcut_icon[index], icon_size);

    u32 label_width = 0;
    gfx_text_size(shortcut_label[index], &label_width, NULL);
    u32 label_x = x + (width > label_width ? (width - label_width) / 2u : 0u);
    u32 label_y = icon_y + icon_size + desktop_px(6u);
    fb_set_clip(x + desktop_px(2u), label_y,
                width > desktop_px(4u) ? width - desktop_px(4u) : 1u,
                desktop_text_height());
    gfx_draw_string(label_x, label_y, shortcut_label[index],
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
    fb_reset_clip();
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;

    /* Preserve the signature blue top edge and leave the rest to the wallpaper. */
    fb_fill_rect(0, 0, g_framebuffer.width, desktop_px(2u), CHARIS_UI_ACCENT);
    u32 work_bottom = g_framebuffer.height > desktop_px(48u)
                    ? g_framebuffer.height - desktop_px(48u) : g_framebuffer.height;
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++) {
        u32 x = 0, y = 0;
        shortcut_position(i, &x, &y);
        if (y >= work_bottom) break;
        draw_shortcut(i);
    }
}

int desktop_surface_hit_test(int mx, int my) {
    u32 width = shortcut_cell_width();
    u32 height = shortcut_cell_height();
    for (u32 i = 0; i < DESKTOP_SHORTCUT_COUNT; i++) {
        u32 x = 0, y = 0;
        shortcut_position(i, &x, &y);
        if (my < (int)y) continue;
        if (mx >= (int)x && mx < (int)(x + width) &&
            my < (int)(y + height))
            return (int)i;
    }
    return -1;
}
