#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/terminal_ascii.h>

int main(void) {
    assert(CHARISOS_ASCII_ROWS == 12u);
    assert(CHARISOS_ASCII_COLUMNS == 19u);
    for (u32 row = 0; row < CHARISOS_ASCII_ROWS; row++) {
        assert(strlen(CHARISOS_ASCII_MARK[row]) == CHARISOS_ASCII_COLUMNS);
    }

    /* The 19-cell mark fits the 712px content area of the 760px default
     * terminal window, even at the maximum supported 3x font scale. */
    assert(CHARISOS_ASCII_COLUMNS * 9u * 3u <= 712u);
    puts("terminal_ascii_layout_test: PASS");
    return 0;
}
