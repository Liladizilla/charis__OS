#include <kernel/mouse.h>
#include <kernel/io.h>
#include <kernel/printf.h>
#include <kernel/irq.h>
#include <kernel/vga.h>

mouse_state_t g_mouse = {50, 50, 0, 0, 0};
static uint32_t mouse_screen_width = 800;
static uint32_t mouse_screen_height = 600;

static inline u64 mouse_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void mouse_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

void mouse_set_bounds(uint32_t width, uint32_t height) {
    /* Coordinates are signed 32-bit values. Do not store a bound that cannot
     * be represented by the cursor, or the IRQ path's comparisons wrap. */
    if (width > 0x7FFFFFFFu) width = 0x7FFFFFFFu;
    if (height > 0x7FFFFFFFu) height = 0x7FFFFFFFu;

    u64 irq_flags = mouse_irq_save();
    mouse_screen_width = width;
    mouse_screen_height = height;

    if (!width) g_mouse.x = 0;
    else if (g_mouse.x >= (s32)width) g_mouse.x = (s32)(width - 1);
    if (!height) g_mouse.y = 0;
    else if (g_mouse.y >= (s32)height) g_mouse.y = (s32)(height - 1);
    mouse_irq_restore(irq_flags);
}

/*
 * PS/2 controller handshake helpers.
 *
 * Controller commands are not all acknowledged like mouse-device commands.
 * In particular, writing 0x20 to port 0x64 returns the controller
 * configuration byte directly through port 0x60. The previous helper drained
 * the output buffer after every command, consuming that response before
 * mouse_init() could read it. Mouse-device commands sent through 0xD4 do
 * return ACK bytes, which ps2_write_aux_data() consumes separately.
 */
static bool ps2_wait_write(void) {
    /* Never write a command after an input-buffer timeout: doing so can splice
     * it into a previous controller/device transaction. */
    for (volatile int i = 0; i < 2000; i++) {
        if (!(inb(0x64) & 0x02)) return true;
    }
    return false;
}

static bool ps2_read_output(bool expect_aux, u8* value) {
    if (!value) return false;

    for (volatile int i = 0; i < 2000; i++) {
        u8 status = inb(0x64);
        if (!(status & 0x01)) continue;

        /* Status bit 5 identifies bytes produced by the auxiliary device.
         * Consume a mismatched byte so it cannot block the controller, but
         * never mistake a keyboard byte for a mouse ACK (or vice versa). */
        bool from_aux = (status & 0x20) != 0;
        u8 data = inb(0x60);
        if (from_aux != expect_aux) return false;
        *value = data;
        return true;
    }
    return false;
}

static void ps2_drain_output(void) {
    /* Drain a bounded number of stale bytes; a missing controller must not
     * turn boot into an unbounded poll loop. */
    for (volatile int i = 0; i < 200; i++) io_delay();
    for (int i = 0; i < 16 && (inb(0x64) & 0x01); i++) {
        (void)inb(0x60);
    }
}

static bool ps2_write_command(u8 cmd) {
    if (!ps2_wait_write()) return false;
    outb(0x64, cmd);
    return true;
}

static bool ps2_write_controller_data(u8 value) {
    if (!ps2_wait_write()) return false;
    outb(0x60, value);
    return true;
}

static bool ps2_write_aux_data(u8 value) {
    /* Mouse commands return 0xFA on success and 0xFE when the device asks
     * for a retry. Validate the AUX source and exact response byte. */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!ps2_write_command(0xD4) || !ps2_write_controller_data(value)) {
            return false;
        }

        u8 response = 0xFF;
        if (!ps2_read_output(true, &response)) return false;
        if (response == 0xFA) return true;
        if (response != 0xFE) return false;
    }
    return false;
}

void mouse_init(void) {
    /* Keep IRQ12 masked until both controller and device handshakes succeed. */
    pic_mask_irq(IRQ_MOUSE);
    ps2_drain_output();

    if (!ps2_write_command(0xA7)) goto failed; /* Disable auxiliary port. */
    ps2_drain_output();
    if (!ps2_write_command(0xA8)) goto failed; /* Enable auxiliary port. */

    /* Controller command 0x20 returns a config byte, not an ACK. */
    u8 config = 0;
    if (!ps2_write_command(0x20) || !ps2_read_output(false, &config)) {
        kprintf("Mouse: controller config read timed out or returned wrong source\\n");
        goto failed;
    }

    /* Enable IRQ12 and clear the auxiliary-clock-disable bit while preserving
     * the controller's other settings. */
    config = (u8)((config | 0x02) & (u8)~0x20);
    if (!ps2_write_command(0x60) || !ps2_write_controller_data(config)) {
        kprintf("Mouse: controller config write timed out\\n");
        goto failed;
    }

    if (!ps2_write_aux_data(0xF6)) {
        kprintf("Mouse: defaults command did not receive AUX ACK\\n");
        goto failed;
    }
    if (!ps2_write_aux_data(0xF4)) {
        kprintf("Mouse: enable-reporting command did not receive AUX ACK\\n");
        goto failed;
    }

    pic_unmask_irq(IRQ_MOUSE);
    kprintf("Mouse: PS/2 enabled, config=0x%02x (F6/F4 ACK)\\n", config);
    return;

failed:
    (void)ps2_write_command(0xA7);
    pic_mask_irq(IRQ_MOUSE);
    kprintf("Mouse: PS/2 initialization failed; IRQ12 remains masked\\n");
}
void mouse_handler(void) {
    /* PS/2 mouse packets are three bytes. Byte 0 always has bit 3 set, which is
     * how the start of a packet is recognised; without that check a stray byte
     * desynchronises the stream and every later packet is garbage. */
    static u8 packet[3];
    static int index = 0;
    static bool overflow_seen = false;

    u8 byte = inb(0x60);

    if (index == 0) {
        /* Wait for a valid header byte. */
        if (!(byte & 0x08)) return;
        /* Overflow bits mean the packet is unreliable; skip it. */
        overflow_seen = (byte & 0xC0) != 0;
    }

    packet[index++] = byte;

    if (index < 3) return;
    index = 0;
    if (overflow_seen) { overflow_seen = false; return; }

    u8 flags = packet[0];
    g_mouse_packets++;
    g_mouse.buttons = flags & 0x07;

    /* Deltas are signed and must be sign-extended, not masked. */
    g_mouse.dx = (s32)(s8)packet[1];
    g_mouse.dy = (s32)(s8)packet[2];

    /* Do the addition in 64 bits so a packet near INT32_MAX cannot trigger
     * signed overflow before the screen clamp is applied. */
    s64 next_x = (s64)g_mouse.x + (s64)g_mouse.dx;
    s64 next_y = (s64)g_mouse.y - (s64)g_mouse.dy;

    if (!mouse_screen_width) next_x = 0;
    else if (next_x < 0) next_x = 0;
    else if (next_x >= (s64)mouse_screen_width)
        next_x = (s64)mouse_screen_width - 1;

    if (!mouse_screen_height) next_y = 0;
    else if (next_y < 0) next_y = 0;
    else if (next_y >= (s64)mouse_screen_height)
        next_y = (s64)mouse_screen_height - 1;

    g_mouse.x = (s32)next_x;
    g_mouse.y = (s32)next_y;
}

bool mouse_has_input(void) {
    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    return mouse.dx != 0 || mouse.dy != 0 || mouse.buttons != 0;
}

int mouse_get_movement(int* dx, int* dy) {
    u64 flags = mouse_irq_save();
    if (dx) *dx = g_mouse.dx;
    if (dy) *dy = g_mouse.dy;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    mouse_irq_restore(flags);
    return 0;
}

int mouse_get_buttons(int* buttons) {
    if (buttons) {
        u64 flags = mouse_irq_save();
        *buttons = g_mouse.buttons;
        mouse_irq_restore(flags);
    }
    return 0;
}

/* Counters and accessors for the shell's input diagnostics. Without these,
 * "the pointer is stuck" is indistinguishable from "no packets ever arrived".
 */
volatile u32 g_mouse_packets = 0;

u32 mouse_packet_count(void) { return g_mouse_packets; }

/* IRQ12 is the sole writer; briefly mask local interrupts while copying so
 * callers never observe coordinates and button bits from different packets. */
void mouse_get_snapshot(mouse_state_t* out) {
    if (!out) return;

    u64 flags = mouse_irq_save();
    out->x = g_mouse.x;
    out->y = g_mouse.y;
    out->dx = g_mouse.dx;
    out->dy = g_mouse.dy;
    out->buttons = g_mouse.buttons;
    mouse_irq_restore(flags);
}

mouse_state_t* mouse_get_state(void) { return &g_mouse; }
