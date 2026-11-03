/* gfx_text.c - Configurable native bitmap typography.
 *
 * Family 0 uses measured proportional advances, family 1 is fixed-cell
 * monospace, and family 2 adds serif terminals to the bundled bitmap glyphs.
 * Every mode is generated from the built-in 8x16 font, so boot rendering has
 * no external filesystem or runtime font dependency.
 */
#include <kernel/gfx_text.h>
#include <kernel/fb.h>
#include <kernel/font.h>
#include <kernel/string.h>
#include <kernel/config.h>

static u32 text_scale = 1;
static u32 text_style = 0;
static u32 ui_scale_percent = 100;

void gfx_text_apply_preferences(void) {
    int style = config_get_int("font_style", 0);
    int scale = config_get_int("font_scale", 1);
    int ui = config_get_int("ui_scale", 100);
    if (style < 0 || style > 2) style = 0;
    if (scale < 1 || scale > 3) scale = 1;
    if (ui != 80 && ui != 100 && ui != 120) ui = 100;
    text_style = (u32)style;
    text_scale = (u32)scale;
    ui_scale_percent = (u32)ui;
}

u32 gfx_text_get_scale(void) { return text_scale; }
u32 gfx_text_get_style(void) { return text_style; }

u32 gfx_ui_scale(u32 logical_pixels) {
    u64 scaled = ((u64)logical_pixels * ui_scale_percent + 50) / 100;
    if (logical_pixels && !scaled) scaled = 1;
    return scaled > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (u32)scaled;
}

static const u8* glyph_data(char ch, u32* left, u32* right) {
    s16 index = font_glyph_index((unsigned char)ch);
    if (index < 0) index = font_glyph_index('?');
    if (index < 0) return NULL;

    const u8* rows = font_glyph_rows((u16)index);
    u32 first = font_cell_w, last = 0;
    bool found = false;
    for (u32 row = 0; row < font_cell_h; row++) {
        u8 bits = rows[row];
        for (u32 col = 0; col < font_cell_w; col++) {
            if (!(bits & (1U << col))) continue;
            if (col < first) first = col;
            if (col > last) last = col;
            found = true;
        }
    }
    if (!found) first = last = 0;
    if (left) *left = first;
    if (right) *right = last;
    return rows;
}

static u32 glyph_advance(char ch, u32 scale) {
    if (ch == ' ') return (u32)(font_cell_w / 2) * scale;
    u32 left = 0, right = (u32)font_cell_w - 1U;
    (void)glyph_data(ch, &left, &right);
    if (text_style == 0) {
        u32 width = right >= left ? right - left + 1 : 1;
        return (width + 1) * scale;
    }
    return (u32)font_cell_w * scale + scale;
}

static void draw_glyph(u32 x, u32 y, char ch, u32 fg, u32 bg, u32 scale) {
    if (!g_framebuffer.initialized || !scale || scale > 8) return;

    u32 left = 0, right = font_cell_w - 1;
    const u8* rows = glyph_data(ch, &left, &right);
    if (!rows) return;

    bool proportional = (text_style == 0);
    u32 source_left = proportional ? left : 0;
    u32 source_right = proportional ? right : ((u32)font_cell_w - 1U);
    u32 advance_width = glyph_advance(ch, scale);
    if (bg != FB_TRANSPARENT)
        fb_fill_rect(x, y, advance_width, (u32)font_cell_h * scale, bg);

    for (u32 row = 0; row < font_cell_h; row++) {
        u8 bits = rows[row];
        for (u32 col = source_left; col <= source_right; col++) {
            if (!(bits & (1U << col))) continue;
            u32 dx = proportional ? col - source_left : col;
            fb_fill_rect(x + dx * scale, y + row * scale, scale, scale, fg);
        }
    }

    /* Bitmap-serif treatment: extend top and baseline terminals by one
     * pixel on either side. This is intentionally conservative at 8x16. */
    if (text_style == 2 && font_cell_w > 2 && font_cell_h > 4) {
        for (u32 col = source_left; col <= source_right; col++) {
            u32 dx = proportional ? col - source_left : col;
            u32 px = x + dx * scale;
            if (rows[0] & (1U << col)) {
                if (dx > 0) fb_fill_rect(px - scale, y, scale, scale, fg);
                if (dx + 1 < (proportional ? source_right - source_left + 1 : font_cell_w))
                    fb_fill_rect(px + scale, y, scale, scale, fg);
            }
            if (rows[font_cell_h - 1] & (1U << col)) {
                u32 py = y + (font_cell_h - 1) * scale;
                if (dx > 0) fb_fill_rect(px - scale, py, scale, scale, fg);
                if (dx + 1 < (proportional ? source_right - source_left + 1 : font_cell_w))
                    fb_fill_rect(px + scale, py, scale, scale, fg);
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
        x += glyph_advance(*s, text_scale);
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
        x += glyph_advance(*s, effective);
    }
}

void gfx_draw_string_centered(u32 y, const char* s, u32 fg, u32 bg) {
    if (!g_framebuffer.initialized || !s) return;
    u32 width = 0;
    for (const char* p = s; *p; p++)
        if (*p != '\n') width += glyph_advance(*p, text_scale);
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
        } else current += glyph_advance(*p, text_scale);
    }
    if (current > longest) longest = current;
    if (out_w) *out_w = longest;
    if (out_h) *out_h = lines * (u32)font_cell_h * text_scale;
}
