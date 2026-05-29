/* input.h - Input event system */
#pragma once
#include <kernel/types.h>

typedef enum {
    INPUT_KEY_DOWN,
    INPUT_KEY_UP,
    INPUT_MOUSE_MOVE,
    INPUT_MOUSE_DOWN,
    INPUT_MOUSE_UP,
    INPUT_MOUSE_SCROLL
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    union {
        struct { int keycode; int scancode; } key;
        struct { int x, y; int dx, dy; int button; } mouse;
    };
    u64 timestamp;
} input_event_t;

#define INPUT_QUEUE_SIZE 256

// Initialize input system
void input_init(void);

// Add event to queue
bool input_push_event(input_event_t* evt);

// Get event from queue (non-blocking)
bool input_pop_event(input_event_t* evt);

// Process all pending events
void input_process_events(void);

// Get mouse state
int input_get_mouse_x(void);
int input_get_mouse_y(void);
bool input_get_mouse_left(void);
bool input_get_mouse_right(void);
bool input_get_mouse_middle(void);