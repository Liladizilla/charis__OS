/* cursor.h - framebuffer software pointer with save-under restoration */
#pragma once
#include <kernel/types.h>

/* Restore the exact pixels beneath the previous pointer before repainting. */
void cursor_erase(void);
/* Draw the current PS/2 pointer only when its position changed. */
void cursor_draw(void);
/* True when the live pointer footprint intersects a clipped framebuffer rectangle. */
bool cursor_intersects_rect(u32 x, u32 y, u32 width, u32 height);
