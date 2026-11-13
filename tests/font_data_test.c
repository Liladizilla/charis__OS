#include <stdio.h>
#include <stdlib.h>
#include <kernel/font.h>

static void expect(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void check_glyph(unsigned char codepoint, int should_have_ink) {
    s16 index = font_glyph_index(codepoint);
    expect(index >= 0 && index < (s16)font_glyph_count,
           "ASCII codepoint maps to a valid glyph");

    const u16* rows = font_glyph_rows((u16)index);
    int any_ink = 0;
    for (u32 y = 0; y < font_cell_h; y++) {
        expect((rows[y] & (u16)~((1u << font_cell_w) - 1u)) == 0,
               "glyph row has no bits outside its cell width");
        if (rows[y]) any_ink = 1;
    }
    expect(any_ink == should_have_ink, "glyph ink presence matches expectation");
}

int main(void) {
    expect(font_cell_w == 12, "UI glyph width is 12 pixels");
    expect(font_cell_h == 22, "UI glyph height is 22 pixels");
    expect(font_glyph_count == 114, "95 ASCII and 19 CP437 glyphs are embedded");
    expect(font_glyph_index(' ') >= 0, "space glyph is present");
    check_glyph(' ', 0);
    check_glyph('A', 1);
    check_glyph('g', 1);
    check_glyph('W', 1);
    check_glyph('0', 1);
    check_glyph('?', 1);

    s16 full_block = font_glyph_index(0xDBu);
    expect(full_block >= 0, "CP437 full block is present");
    const u16* rows = font_glyph_rows((u16)full_block);
    for (u32 y = 0; y < font_cell_h; y++)
        expect(rows[y] == 0x0FFFu, "CP437 full block fills the 12-pixel glyph cell");

    puts("Inter bitmap font tests: PASS");
    return 0;
}
