/* mouse.h - PS/2 mouse driver */
#pragma once
#include <kernel/types.h>

typedef struct {
    int x, y;
    bool left_pressed;
    bool right_pressed;
    bool middle_pressed;
} mouse_state_t;

extern mouse_state_t g_mouse;

void mouse_init(void);
void mouse_handler(void); // Called from IRQ 12

// Poll-based mouse reading (non-blocking)
bool mouse_has_input(void);
int mouse_get_movement(int* dx, int* dy);
int mouse_get_buttons(int* buttons);