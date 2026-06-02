/* gamepad.h - USB HID gamepad support */
#pragma once
#include <kernel/types.h>

typedef struct {
    s16 left_x, left_y;   /* Left stick (-32768 to 32767) */
    s16 right_x, right_y; /* Right stick */
    u8 left_trigger;      /* 0-255 */
    u8 right_trigger;
    u16 buttons;          /* Bitmask of pressed buttons */
} gamepad_state_t;

typedef enum {
    GAMEPAD_BUTTON_A = (1 << 0),
    GAMEPAD_BUTTON_B = (1 << 1),
    GAMEPAD_BUTTON_X = (1 << 2),
    GAMEPAD_BUTTON_Y = (1 << 3),
    GAMEPAD_BUTTON_LB = (1 << 4),
    GAMEPAD_BUTTON_RB = (1 << 5),
    GAMEPAD_BUTTON_BACK = (1 << 6),
    GAMEPAD_BUTTON_START = (1 << 7),
    GAMEPAD_BUTTON_L3 = (1 << 8),
    GAMEPAD_BUTTON_R3 = (1 << 9),
    GAMEPAD_BUTTON_DPAD_UP = (1 << 10),
    GAMEPAD_BUTTON_DPAD_DOWN = (1 << 11),
    GAMEPAD_BUTTON_DPAD_LEFT = (1 << 12),
    GAMEPAD_BUTTON_DPAD_RIGHT = (1 << 13)
} gamepad_button_t;

/* Initialize gamepad subsystem */
void gamepad_init(void);

/* Get current gamepad state */
int gamepad_get_state(gamepad_state_t* state);

/* Check if gamepad is connected */
bool gamepad_is_connected(void);