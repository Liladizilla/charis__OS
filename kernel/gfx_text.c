/* gfx_text.c - Configurable text renderer for the linear framebuffer.
 *
 * The kernel ships a small deterministic bitmap font so the early boot UI
 * never depends on a mounted disk. Appearance preferences transform those
 * glyphs at render time: regular, bold and compact, at integer scales.
 */
#include <kernel/gfx_text.h>
#include <kernel/fb.h>
#include <kernel/font.h>
#include <kernel/string.h>
#include <kernel/config.h>

static u32 text_scale = 1;
static u32 text_style = 0;

void gfx_text_apply_preferences(void) {
    int style = config_get_int("font_style", 0);
    int scale = config_get_int("font_scale", 1);
    if (style < 0 || style > 2) style = 0;
    if (scale < 1 || scale > 3) scale = 1;
    text_style = (u32)style;
    text_scale = (u32)scale;
}

u32 gfx_text_get_scale(void) { return text_scale; }
u32 gfx_text_get_style(void) { return text_style; }

static u32 glyph_width(u32 scale) {
    if (text_style == 2) return ((u32)font_cell_w * scale * 7U) / 8U;
    return (u32)font_cell_w * scale;
}

static void draw_glyph(u32 x, u32 y, char ch, u32 fg, u32 bg, u32 scale) {
    if (!g_framebuffer.initialized || !scale || scale > 8) return;

    s16 idx = font_glyph_index((unsigned char)ch);
    if (idx < 0) idx = font_glyph_index('?');
    if (idx < 0) return;

    const u8* rows = font_glyph_rows((u16)idx);
    for (u32 row = 0; row < font_cell_h; row++) {
        u8 bits = rows[row];
        for (u32 col = 0; col < font_cell_w; col++) {
            u32 px = col * scale;
            if (text_style == 2) px = (col * scale * 7U) / 8U;
            bool on = (bits >> col) & 1U;
            u32 yy = y + row * scale;
            if (on) {
                fb_fill_rect(x + px, yy, scale, scale, fg);
                if (text_style == 1)
                    fb_fill_rect(x + px + scale, yy, scale, scale, fg);
            } else if (bg != FB_TRANSPARENT) {
                fb_fill_rect(x + px, yy, scale, scale, bg);
            }
        }
    }
}

void gfx_draw_char(u32 x, u32 y, char ch, u32 fg, u32 bg) {
    draw_glyph(x, y, ch, fg, bg, text_scale);
}

void gfx_draw_string(u32 x, u32 y, const char* s, u32 fg, u32 bg) {
    if (!g_framebuffer.initialized || !s) return;
    u32 origin_x = x;
    for (; *s; s++) {
        if (*s == '\n') {
            y += (u32)font_cell_h * text_scale;
            x = origin_x;
            continue;
        }
        draw_glyph(x, y, *s, fg, bg, text_scale);
        x += glyph_width(text_scale) + (text_style == 1 ? text_scale : 0);
    }
}

void gfx_draw_string_scaled(u32 x, u32 y, const char* s, u32 fg, u32 bg, u32 scale) {
    if (!g_framebuffer.initialized || !s || !scale) return;
    u32 effective = scale * text_scale;
    if (effective > 8) effective = 8;
    u32 origin_x = x;
    for (; *s; s++) {
        if (*s == '\n') {
            y += (u32)font_cell_h * effective;
            x = origin_x;
            continue;
        }
        draw_glyph(x, y, *s, fg, bg, effective);
        x += glyph_width(effective) + (text_style == 1 ? effective : 0);
    }
}

void gfx_draw_string_centered(u32 y, const char* s, u32 fg, u32 bg) {
    if (!g_framebuffer.initialized || !s) return;
    u32 width = 0;
    for (const char* p = s; *p; p++)
        width += glyph_width(text_scale) + (text_style == 1 ? text_scale : 0);
    u32 x = g_framebuffer.width > width ? (g_framebuffer.width - width) / 2 : 0;
    gfx_draw_string(x, y, s, fg, bg);
}

void gfx_text_size(const char* s, u32* out_w, u32* out_h) {
    if (!s) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    u32 lines = 1, longest = 0, current = 0;
    for (const char* p = s; *p; p++) {
        if (*p == '\n') {
            if (current > longest) longest = current;
            current = 0;
            lines++;
        } else current++;
    }
    if (current > longest) longest = current;
    u32 cell_w = glyph_width(text_scale) + (text_style == 1 ? text_scale : 0);
    if (longest) cell_w = longest * cell_w;
    if (out_w) *out_w = longest ? cell_w : 0;
    if (out_h) *out_h = lines * (u32)font_cell_h * text_scale;
}
