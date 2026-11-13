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

    const u8* pixels = font_glyph_data((u16)index);
    int any_ink = 0;
    int has_partial_coverage = 0;
    for (u32 pixel = 0; pixel < (u32)font_cell_w * font_cell_h; pixel++) {
        expect(pixels[pixel] <= 15, "glyph coverage remains in the 4-bit range");
        if (pixels[pixel]) any_ink = 1;
        if (pixels[pixel] && pixels[pixel] < 15) has_partial_coverage = 1;
    }
    expect(any_ink == should_have_ink, "glyph ink presence matches expectation");
    if (codepoint == 'A')
        expect(has_partial_coverage, "Inter glyph edges contain antialias coverage");
}

int main(void) {
    expect(font_cell_w == 18, "UI glyph width is 18 pixels");
    expect(font_cell_h == 24, "UI glyph height is 24 pixels");
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
    const u8* pixels = font_glyph_data((u16)full_block);
    for (u32 pixel = 0; pixel < (u32)font_cell_w * font_cell_h; pixel++)
        expect(pixels[pixel] == 15, "CP437 full block fills the glyph cell");

    puts("Inter bitmap font tests: PASS");
    return 0;
}
