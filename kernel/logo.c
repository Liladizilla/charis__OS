/* logo.c - CharisOS boot banner
 *
 * One set of lines serves both consoles so they cannot drift apart:
 * logo_print() writes them to the VGA text console, logo_draw_gfx() renders
 * them to the linear framebuffer.
 *
 * ENCODING -- read before editing
 * ------------------------------
 * Both consoles take one byte per character. The text console writes straight
 * into the VGA text buffer, and the framebuffer renderer looks glyphs up in
 * the built-in font by byte value. So these strings carry the raw single-byte
 * CP437 code points, NOT the UTF-8 encodings of the same glyphs: a UTF-8 '╔'
 * is three bytes (E2 95 94) and would render as three unrelated characters.
 *
 * Every non-ASCII glyph is therefore written as an explicit \xNN escape, so
 * the output cannot be altered by the source file's encoding, line endings, or
 * an editor. The mapping:
 *
 *   ╔ 0xC9   ═ 0xCD   ╗ 0xBB   ║ 0xBA
 *   ╚ 0xC8   ╝ 0xBC   █ 0xDB
 */

#include <kernel/logo.h>
#include <kernel/vga.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>

/* Banner palette, chosen to stay legible on the desktop background. */
#define LOGO_FG     FB_ARGB(0xFF, 0xE8, 0xEC, 0xF4)
#define LOGO_ACCENT FB_ARGB(0xFF, 0x6F, 0xC3, 0xFF)
#define LOGO_DIM    FB_ARGB(0xFF, 0x8A, 0x93, 0xA6)
#define LOGO_PANEL  FB_ARGB(0xCC, 0x0D, 0x11, 0x17)

#define H  "\xCD"   /* ═ */
#define V  "\xBA"   /* ║ */
#define B  "\xDB"   /* █ */

static const char* const LOGO_LINES[] = {
    "\xC9" H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
        H H H H H H H H H H H H H H H H H H H H H H H H H H "\xBB",

    V "    " B B B B " " B "   " B "  " B B B "  " B B B B B "  " B B B B
        "  " B B B "   " B B B B "   " V,

    V "   " B "     " B "   " B " " B "   " B " " B "   " B "   "
        B "   " B "     " B "   " B " " B "       " V,

    V "   " B "     " B "   " B " " B "   " B " " B "   " B "   "
        B "   " B "     " B "   " B " " B "       " V,

    V "   " B "     " B B B B B " " B B B B B " " B B B B B "    " B
        "    " B B B "  " B "   " B "  " B B B "    " V,

    V "   " B "     " B "   " B " " B " " B "     " B "   " B "   "
        B "     " B "   " B "   " B "     " B "   " V,

    V "   " B "     " B "   " B " " B "  " B "    " B "       " B " "
        "   " B "   " B "     " B "   " V,

    V "    " B B B B " " B "   " B " " B "   " B " " B B B B B " "
        B B B B "   " B B B "  " B B B B "    " V,

    V "                                                      " V,

    V "  x86_64 kernel  //  built from scratch in C & NASM   " V,

    V "               v1.0  -  Nairobi, Kenya                " V,

    "\xC8" H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
        H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
        H H H H H H H H H H H H H "\xBC",
};

#define LOGO_LINES_N ((u32)(sizeof(LOGO_LINES) / sizeof(LOGO_LINES[0])))
#define LOGO_CELL_H  16
#define LOGO_CELL_W  8

/* Text console. */
void logo_print(void) {
    for (u32 i = 0; i < LOGO_LINES_N; i++) {
        vga_puts(LOGO_LINES[i]);
        vga_puts("\n");
    }
}

/* Framebuffer. Centred horizontally, on a translucent panel so the text stays
 * readable over any wallpaper. */
void logo_draw_gfx(u32 y) {
    if (!g_framebuffer.initialized) return;

    const u32 pad_x = 28;
    const u32 pad_y = 22;

    u32 widest = 0;
    for (u32 i = 0; i < LOGO_LINES_N; i++) {
        u32 w = 0;
        for (const char* p = LOGO_LINES[i]; *p; p++) w += LOGO_CELL_W;
        if (w > widest) widest = w;
    }

    u32 x = (g_framebuffer.width  > widest)
          ? (g_framebuffer.width - widest) / 2
          : pad_x;

    u32 panel_x = (x > pad_x) ? x - pad_x : 0;
    u32 panel_y = (y > pad_y) ? y - pad_y : 0;
    u32 panel_w = widest + pad_x * 2;
    u32 panel_h = LOGO_LINES_N * LOGO_CELL_H + pad_y * 2;

    if (panel_x + panel_w > g_framebuffer.width)
        panel_w = g_framebuffer.width - panel_x;
    if (panel_y + panel_h > g_framebuffer.height)
        panel_h = g_framebuffer.height - panel_y;

    fb_fill_rect(panel_x, panel_y, panel_w, panel_h, LOGO_PANEL);
    fb_draw_rect(panel_x, panel_y, panel_w, panel_h, LOGO_DIM);

    for (u32 i = 0; i < LOGO_LINES_N; i++) {
        u32 fg = LOGO_FG;
        if (i == 8) fg = LOGO_DIM;
        else if (i >= 9) fg = LOGO_ACCENT;
        gfx_draw_string(panel_x + pad_x, panel_y + pad_y + i * LOGO_CELL_H,
                        LOGO_LINES[i], fg, FB_TRANSPARENT);
    }
}
