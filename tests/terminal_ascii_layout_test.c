#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/terminal_ascii.h>

int main(void) {
    assert(CHARISOS_ASCII_ROWS == 6u);
    assert(CHARISOS_ASCII_COLUMNS == 13u);
    for (u32 row = 0; row < CHARISOS_ASCII_ROWS; row++) {
        assert(strlen(CHARISOS_ASCII_MARK[row]) == CHARISOS_ASCII_COLUMNS);
    }

    /* The 13-cell mark uses the terminal's fixed 8px glyph and 1px spacing. */
    assert(CHARISOS_ASCII_COLUMNS * 9u <= 712u);
    puts("terminal_ascii_layout_test: PASS");
    return 0;
}
