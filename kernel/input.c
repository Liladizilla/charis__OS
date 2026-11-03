#include <kernel/input.h>
#include <kernel/mouse.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/memory.h>

static input_event_t input_queue[INPUT_QUEUE_SIZE];
static u32 input_head = 0;
static u32 input_tail = 0;
static volatile u32 input_dropped = 0;

/*
 * Keyboard events are produced from IRQ1, while mouse events are produced
 * by the desktop task. Protect the shared ring against an IRQ interrupting
 * a task-side push/pop. Save and restore the prior IF state so callers that
 * already run with interrupts disabled remain disabled.
 */
static inline u64 input_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void input_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

void input_init(void) {
    u64 flags = input_irq_save();
    input_head = 0;
    input_tail = 0;
    input_dropped = 0;
    input_irq_restore(flags);
}

bool input_push_event(input_event_t* evt) {
    if (!evt) return false;

    u64 flags = input_irq_save();
    u32 next_head = (input_head + 1) % INPUT_QUEUE_SIZE;
    if (next_head == input_tail) {
        input_dropped++;
        input_irq_restore(flags);
        return false; /* Queue full */
    }

    input_queue[input_head] = *evt;
    input_head = next_head;
    input_irq_restore(flags);
    return true;
}

bool input_pop_event(input_event_t* evt) {
    if (!evt) return false;

    u64 flags = input_irq_save();
    if (input_tail == input_head) {
        input_irq_restore(flags);
        return false; /* Queue empty */
    }

    *evt = input_queue[input_tail];
    input_tail = (input_tail + 1) % INPUT_QUEUE_SIZE;
    input_irq_restore(flags);
    return true;
}

void input_process_events(void) {
    /* The graphical shell's raw-scancode hook already enqueues keyboard
     * events. Do not drain the parallel ASCII buffer and enqueue a duplicate
     * character event, which wastes queue capacity during held-key repeats. */
    char c;
    bool have_ascii = keyboard_get_key(&c);
    if (have_ascii && !keyboard_has_key_hook()) {
        input_event_t evt;
        evt.type = INPUT_KEY_DOWN;
        evt.key.keycode = (int)c;
        evt.key.scancode = 0;
        evt.timestamp = timer_get_ms();
        input_push_event(&evt);
    }
    
    /* Process mouse state (simple polling) */
    static int last_x = -1, last_y = -1;
    static uint8_t last_buttons = 0;
    
    if (g_mouse.x != last_x || g_mouse.y != last_y || g_mouse.buttons != last_buttons) {
        input_event_t evt;
        evt.type = INPUT_MOUSE_MOVE;
        evt.mouse.x = g_mouse.x;
        evt.mouse.y = g_mouse.y;
        evt.mouse.dx = g_mouse.dx;
        evt.mouse.dy = g_mouse.dy;
        evt.mouse.button = g_mouse.buttons;
        evt.timestamp = timer_get_ms();
        input_push_event(&evt);
        
        /* Also push click events for button changes */
        uint8_t prev = last_buttons;
        uint8_t cur = g_mouse.buttons;
        for (int btn = 0; btn < 3; btn++) {
            bool was_down = (prev >> btn) & 1;
            bool is_down = (cur >> btn) & 1;
            
            if (!was_down && is_down) {
                /* Button pressed */
                evt.type = INPUT_MOUSE_DOWN;
                evt.mouse.button = btn + 1; /* 1=left, 2=right, 3=middle */
                input_push_event(&evt);
            } else if (was_down && !is_down) {
                /* Button released */
                evt.type = INPUT_MOUSE_UP;
                evt.mouse.button = btn + 1;
                input_push_event(&evt);
            }
        }
        
        last_x = g_mouse.x;
        last_y = g_mouse.y;
        last_buttons = g_mouse.buttons;
    }
}

int input_get_mouse_x(void) {
    return g_mouse.x;
}

int input_get_mouse_y(void) {
    return g_mouse.y;
}

bool input_get_mouse_left(void) {
    return (g_mouse.buttons & 0x01) != 0;
}

bool input_get_mouse_right(void) {
    return (g_mouse.buttons & 0x02) != 0;
}

bool input_get_mouse_middle(void) {
    return (g_mouse.buttons & 0x04) != 0;
}
u32 input_queue_depth(void) {
    u64 flags = input_irq_save();
    u32 depth = (input_head >= input_tail)
              ? (input_head - input_tail)
              : (INPUT_QUEUE_SIZE - (input_tail - input_head));
    input_irq_restore(flags);
    return depth;
}

u32 input_queue_dropped(void) { return input_dropped; }
