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
              kind == 2 ? 0xFF83D6B0u : CHARIS_UI_ACCENT;
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

/* A desktop is a place for icons, not a second application dashboard.
 * The taskbar and launcher own application discovery; the workspace stays
 * quiet and leaves room for real windows. */
static const u32 FILES_ICON_X = 56;
static const u32 FILES_ICON_Y = 64;
static const u32 FILES_CELL_W = 96;
static const u32 FILES_CELL_H = 104;

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;
    u32 x = u(FILES_ICON_X);
    u32 y = u(FILES_ICON_Y);
    u32 size = u(48);
    app_icon(x + (u(FILES_CELL_W)-size)/2u, y, 1, size);
    u32 label_w = 0;
    gfx_text_size("Files", &label_w, NULL);
    gfx_draw_string(x + (u(FILES_CELL_W)-label_w)/2u,
                    y + size + u(12), "Files",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
}

int desktop_surface_hit_test(int mx, int my) {
    u32 x = u(FILES_ICON_X), y = u(FILES_ICON_Y);
    u32 w = u(FILES_CELL_W), h = u(FILES_CELL_H);
    if (mx >= (int)x && mx < (int)(x+w) &&
        my >= (int)y && my < (int)(y+h)) return 0;
    return -1;
}

void desktop_surface_draw_taskbar_icons(u32 y) {
    u32 size = u(28), center = u(44)>size ? (u(44)-size)/2u : 0;
    app_icon(u(112), y+center, 1, size); /* Files */
    app_icon(u(156), y+center, 2, size); /* Terminal */
    app_icon(u(200), y+center, 3, size); /* Settings */
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(108) && mx < (int)u(150)) return 1;
    if (mx >= (int)u(152) && mx < (int)u(194)) return 2;
    if (mx >= (int)u(196) && mx < (int)u(238)) return 3;
    return -1;
}
