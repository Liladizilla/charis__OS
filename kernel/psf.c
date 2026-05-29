#include <kernel/psf.h>
#include <kernel/fb.h>
#include <kernel/string.h>

psf2_header_t* g_psf_font = NULL;
bool g_font_loaded = false;

int psf_load(u8* data) {
    if (!data) return -1;
    
    u32 magic = *(u32*)data;
    if (magic != PSF_MAGIC) return -1;
    
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
    if (c < 32 || c > 127) return; // Skip non-printable
    
    u8* glyph_data = (u8*)g_psf_font + g_psf_font->headersize;
    u32 bytes_per_row = (g_psf_font->width + 7) / 8;
    u32 glyph_offset = (c - 32) * g_psf_font->bytesperglyph; // Start at space (32)
    
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
    if (!g_font_loaded) return;
    
    for (usize i = 0; str[i]; i++) {
        psf_draw_char(x + i * g_psf_font->width, y, str[i], fg, bg);
    }
}