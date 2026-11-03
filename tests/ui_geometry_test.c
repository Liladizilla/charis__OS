#include <kernel/ui_geometry.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* These are the exact display-option rectangles: 576px wide, 38px tall,
     * with 48px between row starts. */
    assert(ui_hit_row(100, 208, 32, 108, 576, 3, 48, 38) == 0);
    assert(ui_hit_row(100, 245, 32, 108, 576, 3, 48, 38) == 0);
    assert(ui_hit_row(100, 246, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 256, 32, 108, 576, 3, 48, 38) == 1);
    assert(ui_hit_row(100, 293, 32, 108, 576, 3, 48, 38) == 1);
    assert(ui_hit_row(100, 294, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 304, 32, 108, 576, 3, 48, 38) == 2);
    assert(ui_hit_row(100, 341, 32, 108, 576, 3, 48, 38) == 2);
    assert(ui_hit_row(31, 208, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(608, 208, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 346, 32, 108, 576, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 208, 32, 108, 0, 3, 48, 38) == -1);
    assert(ui_hit_row(100, 208, 32, 108, 576, 3, 0, 38) == -1);
    puts("UI geometry tests passed");
    return 0;
}
