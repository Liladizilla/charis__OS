/* desktop_surface.c - Calm Precision desktop composition.
 *
 * Keep the working desktop quiet: one useful Files shortcut, no permanent
 * diagnostics cards, no duplicate wordmark, and a consistent icon family.
 * System diagnostics belong in System Monitor, not on the wallpaper.
 */
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/ui_theme.h>

static u32 u(u32 value) { return gfx_ui_scale(value); }
static u32 text_h(void) { return (u32)font_cell_h * gfx_text_get_scale(); }

static void draw_app_icon(u32 x, u32 y, u32 kind, u32 size) {
    u32 inset = u(1);
    u32 plate = CHARIS_UI_SURFACE;
    u32 border = CHARIS_UI_BORDER;
    u32 ink = CHARIS_UI_TEXT;
    if (kind == 0) { plate = 0xFF283B50u; ink = CHARIS_UI_ACCENT; }
    if (kind == 1) { plate = 0xFF3A3021u; ink = 0xFFF0C674u; }
    if (kind == 2) { plate = 0xFF20372Fu; ink = 0xFF83D6B0u; }
    if (kind == 3) { plate = 0xFF29313Bu; ink = CHARIS_UI_TEXT; }

    fb_fill_rect(x + u(2), y + u(2), size, size, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, size, size, plate);
    fb_draw_rect(x, y, size, size, border);

    u32 pad = size / 5;
    u32 inner = size - pad * 2;
    if (kind == 0) {
        /* Browser: simple globe. */
        fb_draw_rect(x + pad, y + pad, inner, inner, ink);
        fb_fill_rect(x + pad, y + size / 2, inner, u(1), ink);
        fb_fill_rect(x + size / 2, y + pad, u(1), inner, ink);
    } else if (kind == 1) {
        /* Files: folder silhouette with a distinct tab. */
        fb_fill_rect(x + pad, y + pad + u(2), inner / 2, u(4), ink);
        fb_fill_rect(x + pad, y + pad + u(5), inner, inner - u(4), ink);
        fb_fill_rect(x + pad + u(4), y + pad + u(10), inner - u(8), u(2), plate);
    } else if (kind == 2) {
        /* Terminal: prompt glyph and baseline. */
        fb_draw_rect(x + pad, y + pad, inner, inner, ink);
        fb_fill_rect(x + pad + u(3), y + pad + inner / 2, inner - u(6), u(1), ink);
        fb_fill_rect(x + pad + u(3), y + pad + inner / 2 + u(4), inner / 2, u(1), ink);
    } else {
        /* Settings: compact mechanical gear. */
        u32 c = x + size / 2, cy = y + size / 2;
        fb_fill_rect(c - u(2), y + pad, u(4), u(5), ink);
        fb_fill_rect(c - u(2), y + size - pad - u(5), u(4), u(5), ink);
        fb_fill_rect(x + pad, cy - u(2), u(5), u(4), ink);
        fb_fill_rect(x + size - pad - u(5), cy - u(2), u(5), u(4), ink);
        fb_draw_rect(c - u(5), cy - u(5), u(10), u(10), ink);
        fb_fill_rect(c - u(2), cy - u(2), u(4), u(4), plate);
    }
    (void)inset;
}

void desktop_surface_render(void) {
    if (!g_framebuffer.initialized) return;

    /* A single home shortcut keeps the wallpaper useful and uncluttered.
     * App launchers live in the taskbar and Start menu. */
    u32 icon = u(48);
    u32 x = u(24);
    u32 y = u(28);
    draw_app_icon(x, y, 1, icon);

    u32 label_width = 0;
    gfx_text_size("Files", &label_width, NULL);
    u32 label_x = x + (icon > label_width ? (icon - label_width) / 2 : 0);
    gfx_draw_string(label_x, y + icon + u(10), "Files",
                    CHARIS_UI_TEXT, FB_TRANSPARENT);
}

/* Desktop shortcut hit-test: 1 is the Files shortcut. */
int desktop_surface_hit_test(int mx, int my) {
    u32 x = u(24), y = u(28), icon = u(48);
    u32 label_h = text_h();
    u32 width = icon > u(64) ? icon : u(64);
    u32 left = x > (width - icon) / 2 ? x - (width - icon) / 2 : 0;
    if (mx < (int)left || mx >= (int)(left + width)) return -1;
    if (my < (int)y || my >= (int)(y + icon + u(10) + label_h)) return -1;
    return 1;
}

/* Four taskbar launchers share the same 28 px app plate and geometry. */
void desktop_surface_draw_taskbar_icons(u32 y) {
    u32 size = u(28);
    draw_app_icon(u(116), y + (u(44) > size ? (u(44) - size) / 2 : 0), 0, size);
    draw_app_icon(u(160), y + (u(44) > size ? (u(44) - size) / 2 : 0), 1, size);
    draw_app_icon(u(204), y + (u(44) > size ? (u(44) - size) / 2 : 0), 2, size);
    draw_app_icon(u(248), y + (u(44) > size ? (u(44) - size) / 2 : 0), 3, size);
}

int desktop_surface_taskbar_hit_test(int mx, int my, u32 taskbar_y) {
    if (my < (int)taskbar_y) return -1;
    if (mx >= (int)u(112) && mx < (int)u(154)) return 0;
    if (mx >= (int)u(156) && mx < (int)u(198)) return 1;
    if (mx >= (int)u(200) && mx < (int)u(242)) return 2;
    if (mx >= (int)u(244) && mx < (int)u(286)) return 3;
    return -1;
}
