/* wallpaper.c - Low-cost solid desktop fields.
 *
 * Each choice uses opaque colours and a small number of geometric planes.
 * This keeps the framebuffer path predictable on low-end hardware and gives
 * text and application surfaces visual priority over decorative effects.
 */

#include <kernel/wallpaper.h>
#include <kernel/fb.h>
#include <kernel/ui_theme.h>

/* Solid desktop fields with restrained geometric planes. No gradient,
 * glow, blur or transparency is required from the kernel graphics path. */
void wallpaper_draw(u32 style) {
    if (!g_framebuffer.initialized) return;
    u32 w = g_framebuffer.width;
    u32 h = g_framebuffer.height;
    u32 base = CHARIS_UI_BACKGROUND;
    u32 plane = 0xFF121A26u;
    u32 edge = 0xFF1B2A40u;
    bool show_plane = false;

    switch (style) {
    case WALLPAPER_EMBER:
        base = 0xFF140B10u; plane = 0xFF21131Au; edge = 0xFF38201Fu;
        show_plane = true;
        break;
    case WALLPAPER_FOREST:
        base = 0xFF0C1713u; plane = 0xFF12231Cu; edge = 0xFF1B352Au;
        show_plane = true;
        break;
    case WALLPAPER_DUSK:
        base = 0xFF120D1Cu; plane = 0xFF1C1530u; edge = 0xFF2B2144u;
        show_plane = true;
        break;
    case WALLPAPER_MONO:
        base = 0xFF101114u; plane = 0xFF17191Eu; edge = 0xFF22262Du;
        show_plane = true;
        break;
    case WALLPAPER_CHARIS:
    case WALLPAPER_COUNT:
    default:
        /* Charis is intentionally a clean graphite field, not an empty
         * giant panel competing with the desktop widgets. */
        break;
    }

    fb_clear(base);
    if (w >= 8 && h >= 8) {
        if (show_plane) {
            u32 plane_x = (w * 3) / 4;
            u32 plane_y = h / 3;
            u32 plane_h = h / 3;
            fb_fill_rect(plane_x, plane_y, w - plane_x, plane_h, plane);
            fb_fill_rect(plane_x, plane_y, 2, plane_h, edge);
            fb_fill_rect(plane_x, plane_y + plane_h - 2, w - plane_x, 2, edge);
        }
        fb_fill_rect(0, h - 2, w, 2, edge);
    }
}

const char* wallpaper_name(u32 style) {
    switch (style) {
    case WALLPAPER_EMBER: return "Ember";
    case WALLPAPER_FOREST: return "Forest";
    case WALLPAPER_DUSK:  return "Dusk";
    case WALLPAPER_MONO:  return "Mono";
    case WALLPAPER_CHARIS:
    default:              return "Charis";
    }
}

const char* wallpaper_description(u32 style) {
    switch (style) {
    case WALLPAPER_EMBER: return "Warm and dark";
    case WALLPAPER_FOREST: return "Cool green";
    case WALLPAPER_DUSK:  return "Deep purple planes";
    case WALLPAPER_MONO:  return "Near-black monochrome";
    case WALLPAPER_CHARIS:
    default:              return "Charis navy";
    }
}

u32 wallpaper_count(void) { return WALLPAPER_COUNT; }
