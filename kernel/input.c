#include <kernel/input.h>
#include <kernel/mouse.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/memory.h>

static input_event_t input_queue[INPUT_QUEUE_SIZE];
static u32 input_head = 0;
static u32 input_tail = 0;

void input_init(void) {
    input_head = 0;
    input_tail = 0;
    mouse_init();
}

bool input_push_event(input_event_t* evt) {
    if (!evt) return false;
    
    u32 next_head = (input_head + 1) % INPUT_QUEUE_SIZE;
    if (next_head == input_tail) {
        return false; // Queue full
    }
    
    input_queue[input_head] = *evt;
    input_head = next_head;
    return true;
}

bool input_pop_event(input_event_t* evt) {
    if (input_tail == input_head) {
        return false; // Queue empty
    }
    
    *evt = input_queue[input_tail];
    input_tail = (input_tail + 1) % INPUT_QUEUE_SIZE;
    return true;
}

void input_process_events(void) {
    // Process keyboard events
    char c;
    if (keyboard_get_key(&c)) {
        input_event_t evt;
        evt.type = INPUT_KEY_DOWN;
        evt.key.keycode = (int)c;
        evt.key.scancode = 0;
        evt.timestamp = timer_get_ms();
        input_push_event(&evt);
    }
    
    // Process mouse state (simple polling)
    static int last_x = -1, last_y = -1;
    static int last_buttons = -1;
    int buttons = (g_mouse.left_pressed ? 1 : 0) |
                  (g_mouse.right_pressed ? 2 : 0) |
                  (g_mouse.middle_pressed ? 4 : 0);
    
    if (g_mouse.x != last_x || g_mouse.y != last_y || buttons != last_buttons) {
        input_event_t evt;
        evt.type = INPUT_MOUSE_MOVE;
        evt.mouse.x = g_mouse.x;
        evt.mouse.y = g_mouse.y;
        evt.mouse.dx = g_mouse.x - last_x;
        evt.mouse.dy = g_mouse.y - last_y;
        evt.mouse.button = buttons;
        evt.timestamp = timer_get_ms();
        input_push_event(&evt);
        last_x = g_mouse.x;
        last_y = g_mouse.y;
        last_buttons = buttons;
    }
}

int input_get_mouse_x(void) {
    return g_mouse.x;
}

int input_get_mouse_y(void) {
    return g_mouse.y;
}

bool input_get_mouse_left(void) {
    return g_mouse.left_pressed;
}

bool input_get_mouse_right(void) {
    return g_mouse.right_pressed;
}

bool input_get_mouse_middle(void) {
    return g_mouse.middle_pressed;
}