#include <kernel/keyboard.h>
#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/memory.h>

static char keyboard_buffer[256];
static u32 keyboard_read_pos = 0;
static u32 keyboard_write_pos = 0;
static bool shift_held = false;

const char keyboard_scancode_set1[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

const char keyboard_scancode_set1_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '-',
    0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_init(void) {
    /* Register IRQ handler */
    irq_register_handler(IRQ_KEYBOARD, keyboard_handler);
    vga_puts("Keyboard initialized\n");
}

/* Optional sink for raw scancodes. The ASCII buffer cannot express keys that
 * have no character -- the Windows/Super key, the arrows, F-keys -- so a
 * graphical shell needs to see the scancode itself. */
static void (*key_event_hook)(u8 scancode, bool pressed) = 0;

void keyboard_set_key_event_hook(void (*hook)(u8 scancode, bool pressed)) {
    key_event_hook = hook;
}

void keyboard_handler(reg_frame_t* frame) {
    (void)frame;
    u8 scancode;
    asm volatile("inb %1, %0" : "=a"(scancode) : "Nd"((u16)KEYBOARD_DATA_PORT));

    // Handle shift keys
    if (scancode == 0x2A || scancode == 0x36) {
        shift_held = true;
        pic_send_eoi(IRQ_KEYBOARD);
        return;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_held = false;
        pic_send_eoi(IRQ_KEYBOARD);
        return;
    }

    /* Extended keys arrive as 0xE0 followed by the real code. Track the prefix
     * so the hook sees a single scancode and can tell press from release
     * (release is the code with bit 7 set). */
    static bool extended = false;
    if (scancode == 0xE0) {
        extended = true;
        pic_send_eoi(IRQ_KEYBOARD);
        return;
    }

    u8 code = scancode & 0x7F;
    bool pressed = (scancode & 0x80) == 0;

    if (key_event_hook) {
        /* Left/right modifiers share codes with the numeric keypad in set 1.
         * The extended prefix is what distinguishes them, so encode it in the
         * high bit when forwarding. */
        key_event_hook((u8)(code | (extended ? 0x80 : 0)), pressed);
    }

    if (pressed && !extended && code < 128) {
        const char* table = shift_held ? keyboard_scancode_set1_shift : keyboard_scancode_set1;
        char c = table[code];
        if (c) {
            keyboard_buffer[keyboard_write_pos] = c;
            keyboard_write_pos = (keyboard_write_pos + 1) % 256;

            /* Wake up any task waiting for input */
            wait_queue_wake();
        }
    }
    extended = false;
    pic_send_eoi(IRQ_KEYBOARD);
}

bool keyboard_get_key(char* out) {
    if (keyboard_read_pos != keyboard_write_pos) {
        *out = keyboard_buffer[keyboard_read_pos];
        keyboard_read_pos = (keyboard_read_pos + 1) % 256;
        return true;
    }
    return false;
}

bool keyboard_has_input(void) {
    return keyboard_read_pos != keyboard_write_pos;
}

void keyboard_read_line(char* buffer, usize max_len) {
    usize i = 0;
    while (i < max_len - 1) {
        char c;
        /* Block if no input available - yield to scheduler */
        while (!keyboard_get_key(&c)) {
            task_t* current = scheduler_current();
            if (current) {
                current->state = TASK_STATE_BLOCKED;
                wait_queue_add(current);
                scheduler_yield();
            }
        }
        if (c == '\n') break;
        buffer[i++] = c;
        vga_putchar(c);
    }
    buffer[i] = 0;
}