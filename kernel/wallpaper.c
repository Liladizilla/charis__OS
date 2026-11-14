/* wallpaper.c - bounded CWP1/CWP2 RGB565 wallpaper renderer. */
#include <kernel/wallpaper.h>
#include <kernel/fb.h>
#include <kernel/bootmem.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>

#define CWP1_MAGIC 0x31505743u /* "CWP1" in little endian */
#define CWP1_VERSION 1u
#define CWP2_MAGIC 0x32505743u /* "CWP2" in little endian */
#define CWP2_VERSION 2u
#define CWP1_MAX_DIMENSION 4096u
#define CWP1_MAX_BYTES (16u * 1024u * 1024u)
#define CWP2_MAX_IMAGES WALLPAPER_COUNT

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u32 version;
    u32 width;
    u32 height;
    u32 stride;
    u32 data_size;
} cwp1_header_t;

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u32 version;
    u32 image_count;
    u32 entry_size;
    u32 total_size;
} cwp2_header_t;

typedef struct __attribute__((packed)) {
    u32 wallpaper_id;
    u32 width;
    u32 height;
    u32 stride;
    u32 data_offset;
    u32 data_size;
} cwp2_entry_t;

static u32 wallpaper_read_magic(const u8* bytes) {
    return (u32)bytes[0] |
           ((u32)bytes[1] << 8) |
           ((u32)bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

static bool wallpaper_render_rgb565(const u8* pixels, u32 source_width,
                                    u32 source_height, u32 source_stride) {
    if (!pixels || !g_framebuffer.initialized ||
        !source_width || !source_height ||
        !g_framebuffer.width || !g_framebuffer.height)
        return false;

    u32 dst_w = g_framebuffer.width;
    u32 dst_h = g_framebuffer.height;

    /* Cover the display while preserving aspect ratio. Integer rational
     * mapping avoids floating point and centers whichever axis is cropped. */
    bool crop_vertical =
        (u64)dst_w * source_height >= (u64)dst_h * source_width;
    u64 crop_numerator = crop_vertical
        ? (u64)dst_w * source_height - (u64)dst_h * source_width
        : (u64)dst_h * source_width - (u64)dst_w * source_height;

    for (u32 y = 0; y < dst_h; y++) {
        u64 source_y = crop_vertical
            ? ((u64)y * source_width + crop_numerator / 2u) / dst_w
            : ((u64)y * source_height) / dst_h;
        if (source_y >= source_height) source_y = source_height - 1u;

        for (u32 x = 0; x < dst_w; x++) {
            u64 source_x = crop_vertical
                ? ((u64)x * source_width) / dst_w
                : ((u64)x * source_height + crop_numerator / 2u) / dst_h;
            if (source_x >= source_width) source_x = source_width - 1u;

            u64 offset = source_y * source_stride + source_x * 2u;
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

static bool wallpaper_draw_cwp1(u32 style, const u8* module,
                                u32 module_size) {
    if (style != WALLPAPER_CHARIS || !module ||
        module_size < sizeof(cwp1_header_t))
        return false;

    cwp1_header_t header;
    kmemcpy(&header, module, sizeof(header));

    if (wallpaper_read_magic(header.magic) != CWP1_MAGIC ||
        header.version != CWP1_VERSION ||
        !header.width || !header.height ||
        header.width > CWP1_MAX_DIMENSION ||
        header.height > CWP1_MAX_DIMENSION ||
        header.width > 0xFFFFFFFFu / 2u ||
        header.stride != header.width * 2u)
        return false;

    u64 required_bytes = (u64)header.stride * header.height;
    if (required_bytes > CWP1_MAX_BYTES ||
        required_bytes != header.data_size ||
        required_bytes > (u64)module_size - sizeof(header))
        return false;

    return wallpaper_render_rgb565(module + sizeof(header), header.width,
                                   header.height, header.stride);
}

static bool wallpaper_draw_cwp2(u32 style, const u8* module,
                                u32 module_size) {
    if (!module || module_size < sizeof(cwp2_header_t))
        return false;

    cwp2_header_t header;
    kmemcpy(&header, module, sizeof(header));
    if (wallpaper_read_magic(header.magic) != CWP2_MAGIC ||
        header.version != CWP2_VERSION ||
        !header.image_count || header.image_count > CWP2_MAX_IMAGES ||
        header.entry_size != sizeof(cwp2_entry_t) ||
        header.total_size != module_size)
        return false;

    u64 directory_bytes = (u64)header.image_count * sizeof(cwp2_entry_t);
    u64 directory_end = sizeof(header) + directory_bytes;
    if (directory_end > module_size)
        return false;

    bool found = false;
    cwp2_entry_t selected = {0};
    for (u32 i = 0; i < header.image_count; i++) {
        cwp2_entry_t entry;
        u64 entry_offset = sizeof(header) + (u64)i * sizeof(entry);
        kmemcpy(&entry, module + entry_offset, sizeof(entry));

        if (entry.wallpaper_id >= WALLPAPER_COUNT ||
            !entry.width || !entry.height ||
            entry.width > CWP1_MAX_DIMENSION ||
            entry.height > CWP1_MAX_DIMENSION ||
            entry.width > 0xFFFFFFFFu / 2u ||
            entry.stride != entry.width * 2u ||
            entry.data_offset < directory_end ||
            entry.data_offset > module_size)
            return false;

        u64 expected_bytes = (u64)entry.stride * entry.height;
        if (expected_bytes > CWP1_MAX_BYTES ||
            expected_bytes != entry.data_size ||
            expected_bytes > (u64)module_size - entry.data_offset)
            return false;

        u64 entry_end = (u64)entry.data_offset + entry.data_size;
        for (u32 j = 0; j < i; j++) {
            cwp2_entry_t previous;
            u64 previous_offset = sizeof(header) +
                                 (u64)j * sizeof(previous);
            kmemcpy(&previous, module + previous_offset, sizeof(previous));

            u64 previous_end = (u64)previous.data_offset + previous.data_size;
            if (previous.wallpaper_id == entry.wallpaper_id ||
                ((u64)entry.data_offset < previous_end &&
                 (u64)previous.data_offset < entry_end))
                return false;
        }

        if (entry.wallpaper_id == style) {
            selected = entry;
            found = true;
        }
    }

    if (!found) return false;
    return wallpaper_render_rgb565(module + selected.data_offset,
                                   selected.width, selected.height,
                                   selected.stride);
}

static bool wallpaper_draw_photo(u32 style) {
    if (!g_framebuffer.initialized || !boot_info.wallpaper_addr ||
        boot_info.wallpaper_size < sizeof(cwp1_header_t) ||
        boot_info.wallpaper_size > BOOTMEM_WALLPAPER_MAX_BYTES)
        return false;

    const u8* module = (const u8*)(uintptr_t)boot_info.wallpaper_addr;
    u32 magic = wallpaper_read_magic(module);
    if (magic == CWP1_MAGIC)
        return wallpaper_draw_cwp1(style, module, boot_info.wallpaper_size);
    if (magic == CWP2_MAGIC)
        return wallpaper_draw_cwp2(style, module, boot_info.wallpaper_size);
    return false;
}

void wallpaper_draw(u32 style) {
    if (!g_framebuffer.initialized) return;
    if (style >= WALLPAPER_COUNT) style = WALLPAPER_CHARIS;
    if (wallpaper_draw_photo(style)) return;

    u32 w = g_framebuffer.width;
    u32 h = g_framebuffer.height;
    u32 base = CHARIS_UI_BACKGROUND;
    u32 plane = 0xFF121A26u;
    u32 edge = 0xFF1B2A40u;
    bool show_plane = false;

    switch (style) {
    case WALLPAPER_CHARIS:
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
    case WALLPAPER_ALPINE:
    case WALLPAPER_FOREST_SKY:
    case WALLPAPER_CITYSCAPE:
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
    case WALLPAPER_ALPINE: return "Alpine Village";
    case WALLPAPER_FOREST_SKY: return "Forest Sky";
    case WALLPAPER_CITYSCAPE: return "Cityscape";
    case WALLPAPER_CHARIS:
    default: return "Charis Lake";
    }
}

const char* wallpaper_description(u32 style) {
    switch (style) {
    case WALLPAPER_EMBER: return "Warm and dark";
    case WALLPAPER_FOREST: return "Cool green";
    case WALLPAPER_DUSK: return "Deep purple planes";
    case WALLPAPER_MONO: return "Near-black monochrome";
    case WALLPAPER_ALPINE: return "Alpine village and mountain lake";
    case WALLPAPER_FOREST_SKY: return "Sunlit forest canopy and open sky";
    case WALLPAPER_CITYSCAPE: return "Paris rooftops at blue hour";
    case WALLPAPER_CHARIS:
    default: return "Lake mountain photograph";
    }
}

u32 wallpaper_count(void) { return WALLPAPER_COUNT; }
