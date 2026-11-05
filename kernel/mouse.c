#include <kernel/mouse.h>
#include <kernel/io.h>
#include <kernel/printf.h>
#include <kernel/irq.h>
#include <kernel/vga.h>

mouse_state_t g_mouse = {50, 50, 0, 0, 0};
static uint32_t mouse_screen_width = 800;
static uint32_t mouse_screen_height = 600;

void mouse_set_bounds(uint32_t width, uint32_t height) {
    mouse_screen_width = width;
    mouse_screen_height = height;

    /* Clamp immediately when a video mode changes, not only after the next
     * mouse packet. Avoid narrowing oversized unsigned dimensions to int. */
    if (!width) g_mouse.x = 0;
    else if (width <= 0x7FFFFFFFu && g_mouse.x >= (s32)width)
        g_mouse.x = (s32)(width - 1);
    if (!height) g_mouse.y = 0;
    else if (height <= 0x7FFFFFFFu && g_mouse.y >= (s32)height)
        g_mouse.y = (s32)(height - 1);
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
    /* Wait for the input buffer to drain (IBF clear). Never write a command
     * after a timeout: doing so can splice it into a previous transaction. */
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
    /* Drain a bounded number of stale bytes. A stuck controller must not
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
    /* Mouse-device commands return 0xFA on success and 0xFE when the device
     * asks for a retry. Validate both the source (AUX) and response byte. */
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
    /* Keep IRQ12 masked until the controller and device have both completed
     * their handshakes. irq_init() registers the handler earlier during boot. */
    pic_mask_irq(12);
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

    pic_unmask_irq(12);
    kprintf("Mouse: PS/2 enabled, config=0x%02x (F6/F4 ACK)\\n", config);
    return;

failed:
    /* Do not leave a half-initialized device generating IRQ12 into a driver
     * whose packet stream has not been started successfully. */
    (void)ps2_write_command(0xA7);
    pic_mask_irq(12);
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

    g_mouse.x += g_mouse.dx;
    g_mouse.y -= g_mouse.dy;   /* PS/2 Y grows upward; screen Y grows down */

    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (mouse_screen_width  && g_mouse.x >= (s32)mouse_screen_width)
        g_mouse.x = mouse_screen_width - 1;
    if (mouse_screen_height && g_mouse.y >= (s32)mouse_screen_height)
        g_mouse.y = mouse_screen_height - 1;
}

bool mouse_has_input(void) {
    return g_mouse.dx != 0 || g_mouse.dy != 0 || g_mouse.buttons != 0;
}

int mouse_get_movement(int* dx, int* dy) {
    if (dx) *dx = g_mouse.dx;
    if (dy) *dy = g_mouse.dy;
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    return 0;
}

int mouse_get_buttons(int* buttons) {
    if (buttons) {
        *buttons = g_mouse.buttons;
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

    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    out->x = g_mouse.x;
    out->y = g_mouse.y;
    out->dx = g_mouse.dx;
    out->dy = g_mouse.dy;
    out->buttons = g_mouse.buttons;
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

mouse_state_t* mouse_get_state(void) { return &g_mouse; }
