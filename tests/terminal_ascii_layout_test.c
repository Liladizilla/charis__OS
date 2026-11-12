#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/terminal_ascii.h>

int main(void) {
    assert(CHARISOS_ASCII_ROWS == 12u);
    assert(CHARISOS_ASCII_COLUMNS == 19u);

    for (u32 row = 0; row < CHARISOS_ASCII_ROWS; row++) {
        const char* line = CHARISOS_ASCII_MARK[row];
        assert(strlen(line) == CHARISOS_ASCII_COLUMNS);
        for (u32 col = 0; col < CHARISOS_ASCII_COLUMNS; col++) {
            assert(line[col] == ' ' || line[col] == '#');
        }
    }

    /* 8px glyph cell + 1px inter-cell spacing, at the maximum 3x text
     * scale. The terminal's default content width is 712px. */
    assert(CHARISOS_ASCII_COLUMNS * 9u * 3u <= 712u);
    puts("terminal_ascii_layout_test: PASS");
    return 0;
}
