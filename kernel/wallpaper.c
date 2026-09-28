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

static const u8 BAYER[16] = {
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5,
};

static u8 dither(u8 base, u32 x, u32 y) {
    int d = (int)BAYER[(y & 3) * 4 + (x & 3)] - 8;
    int v = (int)base + ((d > 4) - (d < -4));
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (u8)v;
}

static void blend(u32 x, u32 y, u32 w, u32 h,
                  u8 t0r, u8 t0g, u8 t0b,
                  u8 t1r, u8 t1g, u8 t1b,
                  u32 horizontal) {
    /* 0..256 along the gradient axis. */
    u32 t;
    if (horizontal) t = (w > 1) ? (x * 256) / (w - 1) : 0;
    else             t = (h > 1) ? (y * 256) / (h - 1) : 0;

    u8 r = (u8)(t0r + ((int)t1r - (int)t0r) * (int)t / 256);
    u8 g = (u8)(t0g + ((int)t1g - (int)t0g) * (int)t / 256);
    u8 b = (u8)(t0b + ((int)t1b - (int)t0b) * (int)t / 256);

    fb_put_pixel(x, y, FB_ARGB(0xFF, dither(r, x, y), dither(g, x, y), dither(b, x, y)));
}

/* A soft diagonal light bloom, added on top of a base wash. */
static void add_bloom(u32 cx, u32 cy, u32 radius, u8 strength) {
    if (!radius) return;
    for (u32 y = 0; y < g_framebuffer.height; y++) {
        for (u32 x = 0; x < g_framebuffer.width; x++) {
            u32 dx = (x > cx) ? x - cx : cx - x;
            u32 dy = (y > cy) ? y - cy : cy - y;
            u32 d2 = dx * dx + dy * dy;
            if (d2 >= radius * radius) continue;

            u32 v = fb_unpack(((u32*)((u8*)(uintptr_t)FB_VIRT_BASE))[y * (g_framebuffer.pitch / 4) + x]);
            u32 a = (u32)((u8)(v >> 16)) + strength;
            u32 g = (u32)((u8)(v >> 8))  + strength;
            u32 b = (u32)((u8)v)          + strength;
            if (a > 255) a = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            fb_put_pixel(x, y, FB_ARGB(0xFF, a, g, b));
        }
    }
}

void wallpaper_draw(u32 style) {
    if (!g_framebuffer.initialized) return;
    u32 w = g_framebuffer.width;
    u32 h = g_framebuffer.height;

    switch (style) {
    case WALLPAPER_CHARIS:
        /* The default: deep navy, lighter towards the bottom. */
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                blend(x, y, w, h, 0x0B, 0x0F, 0x17, 0x1B, 0x2A, 0x46, 0);
        break;

    case WALLPAPER_EMBER:
        /* Warm, dark red to orange. */
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                blend(x, y, w, h, 0x14, 0x08, 0x0C, 0x5A, 0x22, 0x18, 0);
        break;

    case WALLPAPER_FOREST:
        /* Cool green, side to side. */
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                blend(x, y, w, h, 0x06, 0x14, 0x0E, 0x16, 0x40, 0x2A, 1);
        break;

    case WALLPAPER_DUSK:
        /* Purple, with a soft glow off-centre. */
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                blend(x, y, w, h, 0x10, 0x08, 0x1E, 0x38, 0x1C, 0x4E, 0);
        add_bloom(w / 4, h / 3, (w > h ? w : h) / 2, 26);
        break;

    case WALLPAPER_MONO:
        /* Near-black with a faint vertical lift: low power, easy on the eyes. */
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                blend(x, y, w, h, 0x08, 0x08, 0x0A, 0x1A, 0x1A, 0x1E, 0);
        break;

    case WALLPAPER_COUNT:
    default:
        for (u32 y = 0; y < h; y++)
            for (u32 x = 0; x < w; x++)
                blend(x, y, w, h, 0x0B, 0x0F, 0x17, 0x1B, 0x2A, 0x46, 0);
        break;
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
