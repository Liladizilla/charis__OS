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

    /* The mark uses fixed 2x cells; the 14-character identity line still
     * fits beside it when the terminal's text scale is 3x. */
    assert(CHARISOS_ASCII_COLUMNS * 18u <= 712u);
    assert(14u * 27u <= 712u - 18u - CHARISOS_ASCII_COLUMNS * 18u - 24u);
    puts("terminal_ascii_layout_test: PASS");
    return 0;
}
