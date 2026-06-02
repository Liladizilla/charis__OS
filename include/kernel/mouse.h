/* mouse.h - PS/2 mouse driver */
#pragma once
#include <kernel/types.h>

typedef struct {
    int32_t x, y;         /* current cursor position in pixels */
    int32_t dx, dy;       /* last movement delta */
    uint8_t buttons;      /* bitmask: bit 0=left, bit 1=right, bit 2=middle */
} mouse_state_t;

extern mouse_state_t g_mouse;

void mouse_init(void);
void mouse_handler(void); /* Called from IRQ 12 */
void mouse_set_bounds(uint32_t width, uint32_t height);

/* Poll-based mouse reading (non-blocking) */
bool mouse_has_input(void);
int mouse_get_movement(int* dx, int* dy);
int mouse_get_buttons(int* buttons);