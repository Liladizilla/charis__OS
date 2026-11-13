/* font.h - Built-in antialiased font
 *
 * Inter glyph coverage is compiled into the kernel. Runtime text rendering
 * requires neither a mounted filesystem nor a font file.
 */
#ifndef KERNEL_FONT_H
#define KERNEL_FONT_H

#include <kernel/types.h>

extern const u8  font_cell_w;
extern const u8  font_cell_h;
extern const u16 font_first_char;
extern const u16 font_glyph_count;

/* Codepoint -> glyph index, or -1 when the kernel has no glyph for it. */
extern const s16 font_index[256];

/* Row-major 4-bit coverage values, each in the range 0..15. */
const u8* font_glyph_data(u16 index);

/* Convenience: glyph index for a codepoint, or -1. */
static inline s16 font_glyph_index(unsigned char c) {
    return font_index[c];
}

#endif
