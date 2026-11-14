#include <kernel/input.h>
#include <kernel/mouse.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/memory.h>

static input_event_t input_queue[INPUT_QUEUE_SIZE];
static u32 input_head = 0;
static u32 input_tail = 0;
static volatile u32 input_dropped = 0;
static int input_last_x = -1, input_last_y = -1;
static u8 input_last_buttons;

#ifdef INPUT_HOST_TEST
static u64 input_host_irq_state;
static inline u64 input_irq_save(void) {
    u64 previous = input_host_irq_state;
    input_host_irq_state = 1u;
    return previous;
}
static inline void input_irq_restore(u64 flags) {
    input_host_irq_state = flags;
}
#else
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
#endif

void input_init(void) {
    u64 flags = input_irq_save();
    input_head = 0;
    input_tail = 0;
    input_dropped = 0;
    input_last_x = -1;
    input_last_y = -1;
    input_last_buttons = 0u;
#ifdef INPUT_HOST_TEST
    input_host_irq_state = 0u;
#endif
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
    /* With a raw-scancode hook installed, the graphical shell owns the legacy
     * ASCII ring as a second channel: it drains printable characters after
     * dispatching key events so a just-opened modal can receive the same
     * keystroke batch. Draining it here steals those characters before the
     * terminal can read them. Without a raw hook, translate the ring into
     * ordinary input events as before. */
    if (!keyboard_has_key_hook()) {
        char c;
        u32 drained = 0;
        while (drained < 64 && keyboard_get_key(&c)) {
            drained++;
            input_event_t evt;
            evt.type = INPUT_KEY_DOWN;
            evt.key.keycode = (int)c;
            evt.key.scancode = 0;
            evt.timestamp = timer_get_ms();
            input_push_event(&evt);
        }
    }

    /* Read one coherent mouse snapshot. IRQ12 may update the state between
     * ordinary field reads, which can otherwise mix coordinates and buttons
     * from different packets. */
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);

    if (mouse.x != input_last_x || mouse.y != input_last_y ||
        mouse.buttons != input_last_buttons) {
        input_event_t evt;
        evt.type = INPUT_MOUSE_MOVE;
        evt.mouse.x = mouse.x;
        evt.mouse.y = mouse.y;
        evt.mouse.dx = mouse.dx;
        evt.mouse.dy = mouse.dy;
        evt.mouse.button = mouse.buttons;
        evt.timestamp = timer_get_ms();
        input_push_event(&evt);

        /* Also push click events for button changes */
        uint8_t prev = input_last_buttons;
        uint8_t cur = mouse.buttons;
        for (int btn = 0; btn < 5; btn++) {
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

        input_last_x = mouse.x;
        input_last_y = mouse.y;
        input_last_buttons = mouse.buttons;
    }

    s32 scroll_x = 0, scroll_y = 0;
    mouse_take_scroll_deltas(&scroll_x, &scroll_y);
    if (scroll_x || scroll_y) {
        input_event_t evt;
        evt.type = INPUT_MOUSE_SCROLL;
        evt.mouse.x = mouse.x;
        evt.mouse.y = mouse.y;
        evt.mouse.dx = 0;
        evt.mouse.dy = 0;
        evt.mouse.button = 0;
        evt.mouse.scroll_x = scroll_x;
        evt.mouse.scroll_y = scroll_y;
        evt.timestamp = timer_get_ms();
        input_push_event(&evt);
    }
}

int input_get_mouse_x(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return mouse.x;
}

int input_get_mouse_y(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return mouse.y;
}

bool input_get_mouse_left(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return (mouse.buttons & 0x01) != 0;
}

bool input_get_mouse_right(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return (mouse.buttons & 0x02) != 0;
}

bool input_get_mouse_middle(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return (mouse.buttons & 0x04) != 0;
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
