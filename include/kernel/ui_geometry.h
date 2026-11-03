/* ui_geometry.h - Shared integer hit-testing primitives for native UI. */
#pragma once
#include <kernel/types.h>

/* Return the row whose drawn rectangle contains (mx,my), or -1.
 * pitch is the vertical distance between row starts; height is the clickable
 * height within each pitch. Keeping both explicit avoids hit targets drifting
 * away from their rendered controls. */
static inline int ui_hit_row(int mx, int my, u32 x, u32 y, u32 width,
                             u32 count, u32 pitch, u32 height) {
    if (!width || !count || !pitch || !height || height > pitch) return -1;
    if (mx < (int)x || mx >= (int)(x + width) || my < (int)y) return -1;
    u32 relative = (u32)(my - (int)y);
    u32 row = relative / pitch;
    if (row >= count || relative % pitch >= height) return -1;
    return (int)row;
}
