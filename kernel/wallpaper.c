/* wallpaper.c - bounded CWP1 boot-module wallpaper renderer. */
#include <kernel/wallpaper.h>
#include <kernel/fb.h>
#include <kernel/bootmem.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>

#define CWP1_MAGIC 0x31505743u /* "CWP1" in little endian */
#define CWP1_VERSION 1u
#define CWP1_MAX_DIMENSION 4096u
#define CWP1_MAX_BYTES (16u * 1024u * 1024u)

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u32 version;
    u32 width;
    u32 height;
    u32 stride;
    u32 data_size;
} cwp1_header_t;

static bool wallpaper_draw_photo(void) {
    if (!g_framebuffer.initialized || !boot_info.wallpaper_addr ||
        boot_info.wallpaper_size < sizeof(cwp1_header_t))
        return false;

    const u8* module = (const u8*)(uintptr_t)boot_info.wallpaper_addr;
    cwp1_header_t header;
    kmemcpy(&header, module, sizeof(header));

    u32 magic = (u32)header.magic[0] |
                ((u32)header.magic[1] << 8) |
                ((u32)header.magic[2] << 16) |
                ((u32)header.magic[3] << 24);
    if (magic != CWP1_MAGIC || header.version != CWP1_VERSION ||
        !header.width || !header.height ||
        header.width > CWP1_MAX_DIMENSION ||
        header.height > CWP1_MAX_DIMENSION ||
        header.width > 0xFFFFFFFFu / 2u ||
        header.stride != header.width * 2u)
        return false;

    u64 required_bytes = (u64)header.stride * header.height;
    if (required_bytes > CWP1_MAX_BYTES ||
        required_bytes != header.data_size ||
        required_bytes > (u64)boot_info.wallpaper_size - sizeof(header))
        return false;

    const u8* pixels = module + sizeof(header);
    u32 dst_w = g_framebuffer.width;
    u32 dst_h = g_framebuffer.height;
    if (!dst_w || !dst_h) return false;

    /* Cover the display while preserving aspect ratio. Integer rational
     * mapping avoids floating point and centers whichever axis is cropped. */
    bool crop_vertical =
        (u64)dst_w * header.height >= (u64)dst_h * header.width;
    u64 crop_numerator = crop_vertical
        ? (u64)dst_w * header.height - (u64)dst_h * header.width
        : (u64)dst_h * header.width - (u64)dst_w * header.height;

    for (u32 y = 0; y < dst_h; y++) {
        u64 source_y = crop_vertical
            ? ((u64)y * header.width + crop_numerator / 2u) / dst_w
            : ((u64)y * header.height) / dst_h;
        if (source_y >= header.height) source_y = header.height - 1u;

        for (u32 x = 0; x < dst_w; x++) {
            u64 source_x = crop_vertical
                ? ((u64)x * header.width) / dst_w
                : ((u64)x * header.height + crop_numerator / 2u) / dst_h;
            if (source_x >= header.width) source_x = header.width - 1u;

            u64 offset = source_y * header.stride + source_x * 2u;
            u16 rgb565 = (u16)pixels[offset] |
                         ((u16)pixels[offset + 1u] << 8);
            u32 red5 = (rgb565 >> 11) & 0x1Fu;
            u32 green6 = (rgb565 >> 5) & 0x3Fu;
            u32 blue5 = rgb565 & 0x1Fu;
            u32 red = (red5 << 3) | (red5 >> 2);
            u32 green = (green6 << 2) | (green6 >> 4);
            u32 blue = (blue5 << 3) | (blue5 >> 2);
            fb_put_pixel(x, y, FB_ARGB(0xFF, red, green, blue));
        }
    }
    return true;
}

void wallpaper_draw(u32 style) {
    if (!g_framebuffer.initialized) return;
    u32 w = g_framebuffer.width;
    u32 h = g_framebuffer.height;
    u32 base = CHARIS_UI_BACKGROUND;
    u32 plane = 0xFF121A26u;
    u32 edge = 0xFF1B2A40u;
    bool show_plane = false;

    switch (style) {
    case WALLPAPER_CHARIS:
        if (wallpaper_draw_photo()) return;
        break;
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
    case WALLPAPER_COUNT:
    default:
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
    case WALLPAPER_DUSK: return "Dusk";
    case WALLPAPER_MONO: return "Mono";
    case WALLPAPER_CHARIS:
    default: return "Charis";
    }
}

const char* wallpaper_description(u32 style) {
    switch (style) {
    case WALLPAPER_EMBER: return "Warm and dark";
    case WALLPAPER_FOREST: return "Cool green";
    case WALLPAPER_DUSK: return "Deep purple planes";
    case WALLPAPER_MONO: return "Near-black monochrome";
    case WALLPAPER_CHARIS:
    default: return "Mountain lake photograph";
    }
}

u32 wallpaper_count(void) { return WALLPAPER_COUNT; }
