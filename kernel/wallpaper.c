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
        /* Layered planes keep the wallpaper cheap while giving the shell a
         * little depth. The restrained star field adds texture without
         * requiring a large embedded bitmap or expensive per-pixel effects. */
        u32 plane_x = (w * 2) / 3;
        fb_fill_rect(plane_x, h / 8, w - plane_x, (h * 3) / 4, plane);
        fb_fill_rect(0, h - 6, w, 6, edge);
        fb_fill_rect(plane_x, h / 8, 2, (h * 3) / 4, edge);

        u32 seed = 0xC4A2157Bu;
        for (u32 i = 0; i < 96; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            u32 sx = seed % w;
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            u32 sy = seed % ((h * 3) / 4 ? (h * 3) / 4 : 1);
            u32 shade = (i % 7 == 0) ? 0xFF8CB9E8u
                       : (i % 3 == 0) ? 0xFF526B8Au : 0xFF34445Eu;
            fb_put_pixel(sx, sy, shade);
            if (i % 11 == 0 && sx + 1 < w && sy + 1 < h)
                fb_fill_rect(sx, sy, 2, 2, 0xFF526B8Au);
        }
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
