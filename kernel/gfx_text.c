/* gfx_text.c - Text rendering on the linear framebuffer
 *
 * Uses the font compiled into the kernel (font_data.c) rather than a PSF file
 * on disk: there may be no readable disk at all when the desktop first draws,
 * and a kernel should not depend on a filesystem to print its own UI.
 */

#include <kernel/gfx_text.h>
#include <kernel/fb.h>
#include <kernel/font.h>
#include <kernel/string.h>

/* Draw one glyph. `fg`/`bg` are 0xAARRGGBB; pass FB_TRANSPARENT for bg to
 * leave the background alone, which is what a status bar wants. */
void gfx_draw_char(u32 x, u32 y, char ch, u32 fg, u32 bg) {
    if (!g_framebuffer.initialized) return;

    s16 idx = font_glyph_index((unsigned char)ch);
    if (idx < 0) idx = font_glyph_index('?');
    if (idx < 0) return;

    const u8* rows = font_glyph_rows((u16)idx);
    u8 cw = font_cell_w;
    u8 chh = font_cell_h;

    for (u32 row = 0; row < chh; row++) {
        u8 bits = rows[row];
        for (u32 col = 0; col < cw; col++) {
            bool on = (bits >> col) & 1;
            if (on) {
                fb_put_pixel(x + col, y + row, fg);
            } else if (bg != FB_TRANSPARENT) {
                fb_put_pixel(x + col, y + row, bg);
            }
        }
    }
}

void gfx_draw_string(u32 x, u32 y, const char* s, u32 fg, u32 bg) {
    if (!g_framebuffer.initialized || !s) return;
    u32 cursor = x;
    for (; *s; s++) {
        if (*s == '\n') { y += font_cell_h; cursor = x; continue; }
        gfx_draw_char(cursor, y, *s, fg, bg);
        cursor += font_cell_w;
    }
}

/* Centre a string horizontally on the screen. */
void gfx_draw_string_centered(u32 y, const char* s, u32 fg, u32 bg) {
    if (!g_framebuffer.initialized || !s) return;
    u32 len = (u32)kstrlen(s);
    u32 width = len * font_cell_w;
    u32 x = (g_framebuffer.width > width) ? (g_framebuffer.width - width) / 2 : 0;
    gfx_draw_string(x, y, s, fg, bg);
}

void gfx_text_size(const char* s, u32* out_w, u32* out_h) {
    if (!s) { if (out_w) *out_w = 0; if (out_h) *out_h = 0; return; }
    u32 lines = 1;
    u32 longest = 0;
    u32 cur = 0;
    for (const char* p = s; *p; p++) {
        if (*p == '\n') { if (cur > longest) longest = cur; cur = 0; lines++; }
        else cur++;
    }
    if (cur > longest) longest = cur;
    if (out_w) *out_w = longest * font_cell_w;
    if (out_h) *out_h = lines * font_cell_h;
}
