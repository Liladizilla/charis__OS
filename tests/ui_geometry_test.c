#include <kernel/ui_geometry.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* These are the exact display-option rectangles: 576px wide, 38px tall,
     * with 48px between row starts. */
    assert(ui_hit_row(100, 108, 32, 108, 576, 3, 48, 38) == 0);
    assert(ui_hit_row(100, 145, 32, 108, 576, 3, 48, 38) == 0);
    assert(ui_hit_row(100, 146, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 156, 32, 108, 576, 3, 48, 38) == 1);
    assert(ui_hit_row(100, 193, 32, 108, 576, 3, 48, 38) == 1);
    assert(ui_hit_row(100, 194, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 204, 32, 108, 576, 3, 48, 38) == 2);
    assert(ui_hit_row(100, 241, 32, 108, 576, 3, 48, 38) == 2);
    assert(ui_hit_row(31, 108, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(608, 108, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 242, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 108, 32, 108, 0, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 108, 32, 108, 576, 3, 0, 38) == -1);
    puts("UI geometry tests passed");
    return 0;
}
