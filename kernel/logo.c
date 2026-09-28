/* logo.c - CharisOS boot banner
 *
 * Printed once from kernel_main() at the end of initialisation, once the
 * boot-progress markers have stopped writing to row 0.
 *
 * ENCODING -- read before editing
 * ------------------------------
 * vga_putchar() writes one byte per character straight into the VGA text
 * buffer. It does not decode UTF-8. So these strings must carry the raw
 * single-byte CP437 code points that VGA text mode's character generator
 * draws, NOT the UTF-8 encodings of the same glyphs. A UTF-8 '╔' is three
 * bytes (E2 95 94) and would render as three unrelated characters.
 *
 * Every non-ASCII glyph below is therefore written as an explicit \xNN escape
 * so the output cannot be changed by the source file's encoding, line endings,
 * or an editor. The mapping:
 *
 *   ╔ 0xC9    ═ 0xCD    ╗ 0xBB    ║ 0xBA
 *   ╚ 0xC8    ╝ 0xBC    █ 0xDB
 *
 * The frame is 55 columns, so it fits comfortably in 80x25 alongside the
 * init log.
 */

#include <kernel/logo.h>
#include <kernel/vga.h>

#define TL "\xC9"   /* ╔ */
#define TR "\xBB"   /* ╗ */
#define BL "\xC8"   /* ╚ */
#define BR "\xBC"   /* ╝ */
#define H  "\xCD"   /* ═ */
#define V  "\xBA"   /* ║ */
#define B  "\xDB"   /* █ */

void logo_print(void) {
    vga_puts(TL H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
                H H H H H H H H H H H H H H H H H H H H H H TR "\n");
    vga_puts(V "    " B B B B " " B "   " B "  " B B B "  " B B B B B "  "
                B B B B "  " B B B "   " B B B B "   " V "\n");
    vga_puts(V "   " B "     " B "   " B " " B "   " B " " B "   " B "   "
                B "   " B "     " B "   " B " " B "       " V "\n");
    vga_puts(V "   " B "     " B "   " B " " B "   " B " " B "   " B "   "
                B "   " B "     " B "   " B " " B "       " V "\n");
    vga_puts(V "   " B "     " B B B B B " " B B B B " " B B B B "    " B
                "    " B B B "  " B "   " B "  " B B B "    " V "\n");
    vga_puts(V "   " B "     " B "   " B " " B " " B "     " B "   " B "   "
                B "     " B "   " B "   " B "     " B "   " V "\n");
    vga_puts(V "   " B "     " B "   " B " " B "  " B "    " B "       " B " "
                "   " B "   " B "     " B "   " V "\n");
    vga_puts(V "    " B B B B " " B "   " B " " B "   " B " " B B B B B " "
                B B B B "   " B B B "  " B B B B "    " V "\n");
    vga_puts(V "                                                      " V "\n");
    vga_puts(V "  x86_64 kernel  //  built from scratch in C & NASM   " V "\n");
    vga_puts(V "               v1.0  -  Nairobi, Kenya                " V "\n");
    vga_puts(BL H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
                H H H H H H H H H H H H H H H H H H H H H H BR "\n");
}
