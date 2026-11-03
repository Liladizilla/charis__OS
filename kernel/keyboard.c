#include <kernel/keyboard.h>
#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/memory.h>
#include <kernel/io.h>

static char keyboard_buffer[256];
static u32 keyboard_read_pos = 0;
static u32 keyboard_write_pos = 0;
static bool shift_held = false;
volatile u32 g_keyboard_irqs = 0;
static bool keyboard_ready = false;

u32 keyboard_irq_count(void) { return g_keyboard_irqs; }
bool keyboard_controller_ready(void) { return keyboard_ready; }

static bool ps2_wait_write(void) {
    for (volatile u32 i = 0; i < 4000; i++) {
        if (!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_INBUF)) return true;
    }
    return false;
}

static bool ps2_wait_read(u8* value) {
    if (!value) return false;
    for (volatile u32 i = 0; i < 4000; i++) {
        u8 status = inb(KEYBOARD_STATUS_PORT);
        if (status & KEYBOARD_STATUS_OUTBUF) {
            u8 data = inb(KEYBOARD_DATA_PORT);
            /* The auxiliary port can share the controller output queue. */
            if (status & 0x20) continue;
            *value = data;
            return true;
        }
    }
    return false;
}

static void ps2_drain_keyboard_output(void) {
    for (volatile u32 i = 0; i < 32; i++) {
        if (!(inb(KEYBOARD_STATUS_PORT) & KEYBOARD_STATUS_OUTBUF)) break;
        (void)inb(KEYBOARD_DATA_PORT);
    }
}

static bool ps2_controller_command(u8 command) {
    if (!ps2_wait_write()) return false;
    outb(KEYBOARD_COMMAND_PORT, command);
    return true;
}

static bool ps2_keyboard_command(u8 command) {
    if (!ps2_wait_write()) return false;
    outb(KEYBOARD_DATA_PORT, command);
    u8 response = 0;
    if (!ps2_wait_read(&response)) return false;
    if (response == 0xFA) return true;
    if (response == 0xFE && ps2_wait_write()) {
        outb(KEYBOARD_DATA_PORT, command);
        return ps2_wait_read(&response) && response == 0xFA;
    }
    return false;
}

/* Configure the first 8042 port instead of merely unmasking IRQ1. Firmware
 * settings are preserved where possible; translation and IRQ1 are explicitly
 * enabled so the set-1 decoder receives the format it understands. */
static bool keyboard_controller_init(void) {
    ps2_drain_keyboard_output();
    if (!ps2_controller_command(0xAD)) return false; /* disable port 1 */
    ps2_drain_keyboard_output();

    u8 config = 0;
    bool config_ok = ps2_controller_command(0x20) && ps2_wait_read(&config);
    if (config_ok) {
        config &= (u8)~0x10; /* enable first-port clock */
        config |= 0x41;      /* IRQ1 + set-1 translation */
        if (!ps2_controller_command(0x60) || !ps2_wait_write()) return false;
        outb(KEYBOARD_DATA_PORT, config);
    }

    if (!ps2_controller_command(0xAE)) return false; /* enable port 1 */

    /* Stop any inherited key stream, then explicitly enable device scanning. */
    bool scanning = ps2_keyboard_command(0xF5);
    bool enabled = ps2_keyboard_command(0xF4);
    ps2_drain_keyboard_output();
    return config_ok && scanning && enabled;
}

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
    keyboard_ready = keyboard_controller_init();
    /* Register the ISR even when the controller did not answer; the settings
     * panel can then distinguish missing hardware from an unhandled IRQ. */
    irq_register_handler(IRQ_KEYBOARD, keyboard_handler);
    if (keyboard_ready) vga_puts("Keyboard: PS/2 controller and scanning enabled\n");
    else vga_puts_error("Keyboard: PS/2 initialization incomplete; input may be unavailable");
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
    g_keyboard_irqs++;
    u8 scancode;
    asm volatile("inb %1, %0" : "=a"(scancode) : "Nd"((u16)KEYBOARD_DATA_PORT));

    // Forward modifier transitions too; graphical consumers need them to
    // maintain their own key state, even though Shift has no standalone ASCII.
    if (scancode == 0x2A || scancode == 0x36 ||
        scancode == 0xAA || scancode == 0xB6) {
        bool pressed = (scancode & 0x80) == 0;
        if (key_event_hook) {
            key_event_hook((u8)(scancode & 0x7F), pressed);
        }
        shift_held = pressed;
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
            u32 next_write_pos = (keyboard_write_pos + 1) % 256;
            /* Keep one slot empty to distinguish full from empty. Drop the
             * newest key if the consumer has not caught up, rather than
             * wrapping the indices and making queued input look empty. */
            if (next_write_pos != keyboard_read_pos) {
                keyboard_buffer[keyboard_write_pos] = c;
                keyboard_write_pos = next_write_pos;

                /* Wake up any task waiting for input */
                wait_queue_wake();
            }
        }
    }
    extended = false;
    pic_send_eoi(IRQ_KEYBOARD);
}

bool keyboard_get_key(char* out) {
    if (!out) return false;
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
    if (!buffer || max_len == 0) return;
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
bool keyboard_has_key_hook(void) {
    return key_event_hook != 0;
}
