/* wallpaper.c - Procedural desktop backgrounds
 *
 * Generated rather than stored: a handful of gradients, each with its own
 * geometry, all cheap to compute and none of them needing image data in the
 * kernel. The wizard previews them and the desktop uses whichever was chosen.
 *
 * 8-bit colour makes a wide gradient band visibly, so every ramp is dithered
 * with a 4x4 ordered matrix, which pushes adjacent pixels a shade apart and
 * hides the steps.
 */

#include <kernel/wallpaper.h>
#include <kernel/fb.h>
#include <kernel/printf.h>

/* Solid desktop fields with restrained geometric planes. No gradient,
 * glow, blur or transparency is required from the kernel graphics path. */
void wallpaper_draw(u32 style) {
    if (!g_framebuffer.initialized) return;
    u32 w = g_framebuffer.width;
    u32 h = g_framebuffer.height;
    u32 base = 0xFF0D1117u;
    u32 plane = 0xFF121A26u;
    u32 edge = 0xFF1B2A40u;

    switch (style) {
    case WALLPAPER_EMBER:
        base = 0xFF140B10u; plane = 0xFF21131Au; edge = 0xFF38201Fu;
        break;
    case WALLPAPER_FOREST:
        base = 0xFF0C1713u; plane = 0xFF12231Cu; edge = 0xFF1B352Au;
        break;
    case WALLPAPER_DUSK:
        base = 0xFF120D1Cu; plane = 0xFF1C1530u; edge = 0xFF2B2144u;
        break;
    case WALLPAPER_MONO:
        base = 0xFF101114u; plane = 0xFF17191Eu; edge = 0xFF22262Du;
        break;
    case WALLPAPER_CHARIS:
    case WALLPAPER_COUNT:
    default:
        break;
    }

    fb_clear(base);
    if (w >= 8 && h >= 8) {
        /* A quiet, right-aligned plane gives the desktop depth without
         * competing with text or pretending to be a photographed wallpaper. */
        u32 plane_x = (w * 2) / 3;
        fb_fill_rect(plane_x, h / 8, w - plane_x, (h * 3) / 4, plane);
        fb_fill_rect(0, h - 6, w, 6, edge);
        fb_fill_rect(plane_x, h / 8, 2, (h * 3) / 4, edge);
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
    case WALLPAPER_DUSK:  return "Purple with a glow";
    case WALLPAPER_MONO:  return "Dim, low power";
    case WALLPAPER_CHARIS:
    default:              return "Charis navy";
    }
}

u32 wallpaper_count(void) { return WALLPAPER_COUNT; }
