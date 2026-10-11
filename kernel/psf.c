#include <kernel/psf.h>
#include <kernel/fb.h>
#include <kernel/string.h>

psf2_header_t* g_psf_font = NULL;
bool g_font_loaded = false;

int psf_load(u8* data) {
    if (!data) return -1;
    
    /* Modules are byte-addressed; avoid assuming the header is aligned. */
    u32 magic;
    kmemcpy(&magic, data, sizeof(magic));
    if (magic != PSF_MAGIC) return -1;

    psf2_header_t header;
    kmemcpy(&header, data, sizeof(header));
    u64 row_bytes = ((u64)header.width + 7) / 8;
    u64 minimum_glyph_bytes = row_bytes * header.height;

    /* The module size is supplied by Multiboot, but this API currently only
     * receives its address. Validate every internal size we can validate
     * before allowing dimensions to drive drawing loops. */
    if (header.headersize < sizeof(psf2_header_t) ||
        !header.numglyph || !header.bytesperglyph ||
        !header.width || !header.height ||
        header.width > 64 || header.height > 64 ||
        minimum_glyph_bytes > header.bytesperglyph ||
        header.numglyph > (0xFFFFFFFFULL - header.headersize) / header.bytesperglyph)
        return -1;

    g_psf_font = (psf2_header_t*)data;
    g_font_loaded = true;
    
    return 0;
}

u32 psf_char_width(void) {
    return g_font_loaded ? g_psf_font->width : 8;
}

u32 psf_char_height(void) {
    return g_font_loaded ? g_psf_font->height : 16;
}

void psf_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    if (!g_font_loaded || !g_framebuffer.initialized) return;
    if ((unsigned char)c < 32 || (unsigned char)c > 127) return; // Skip non-printable
    if ((u32)((unsigned char)c - 32) >= g_psf_font->numglyph) return;

    u8* glyph_data = (u8*)g_psf_font + g_psf_font->headersize;
    u32 bytes_per_row = (g_psf_font->width + 7) / 8;
    u32 glyph_offset = ((u32)(unsigned char)c - 32) * g_psf_font->bytesperglyph;
    
    for (u32 row = 0; row < g_psf_font->height; row++) {
        for (u32 col = 0; col < g_psf_font->width; col++) {
            u32 byte_idx = row * bytes_per_row + (col / 8);
            u32 bit_idx = 7 - (col % 8);
            
            bool pixel_on = glyph_data[glyph_offset + byte_idx] & (1 << bit_idx);
            fb_put_pixel(x + col, y + row, pixel_on ? fg : bg);
        }
    }
}

void psf_draw_string(u32 x, u32 y, const char* str, u32 fg, u32 bg) {
    if (!g_font_loaded || !str || !g_framebuffer.initialized) return;
    /* The framebuffer API exposes a direct pixel buffer only in 32bpp mode.
     * Use the clipped per-pixel path when no such buffer exists. */
    if (!g_framebuffer.pixels) {
        for (usize i = 0; str[i]; i++) {
            u64 draw_x = (u64)x + i * g_psf_font->width;
            if (draw_x >= g_framebuffer.width) break;
            psf_draw_char((u32)draw_x, y, str[i], fg, bg);
        }
        return;
    }
    psf_draw_string_buffer(x, y, str, fg, bg, g_framebuffer.pixels, g_framebuffer.width);
}

void psf_draw_string_buffer(u32 x, u32 y, const char* str, u32 fg, u32 bg, u32* buffer, u32 buf_width) {
    if (!g_font_loaded || !buffer || !str || !buf_width) return;

    for (usize i = 0; str[i]; i++) {
        u64 draw_x = (u64)x + i * g_psf_font->width;
        if (draw_x >= buf_width) break;
        psf_draw_char_buffer((u32)draw_x, y, str[i], fg, bg, buffer, buf_width);
    }
}

void psf_draw_char_buffer(u32 x, u32 y, char c, u32 fg, u32 bg, u32* buffer, u32 buf_width) {
    if (!g_font_loaded || !buffer || !buf_width) return;
    if ((unsigned char)c < 32 || (unsigned char)c > 127) return; /* Skip non-printable */
    if ((u32)((unsigned char)c - 32) >= g_psf_font->numglyph) return;
    if (x >= buf_width || y >= g_framebuffer.height) return;

    u8* glyph_data = (u8*)g_psf_font + g_psf_font->headersize;
    u32 bytes_per_row = (g_psf_font->width + 7) / 8;
    u32 glyph_offset = ((u32)(unsigned char)c - 32) * g_psf_font->bytesperglyph;
    
    for (u32 row = 0; row < g_psf_font->height; row++) {
        for (u32 col = 0; col < g_psf_font->width; col++) {
            u32 byte_idx = row * bytes_per_row + (col / 8);
            u32 bit_idx = 7 - (col % 8);
            
            bool pixel_on = glyph_data[glyph_offset + byte_idx] & (1 << bit_idx);
            /* Subtraction-based clipping prevents coordinate addition from
             * wrapping around into the visible framebuffer. */
            if (col < buf_width - x && row < g_framebuffer.height - y) {
                u64 pixel_index = (u64)(y + row) * buf_width + (x + col);
                if (pixel_index < (u64)buf_width * g_framebuffer.height)
                    buffer[pixel_index] = pixel_on ? fg : bg;
            }
        }
    }
}