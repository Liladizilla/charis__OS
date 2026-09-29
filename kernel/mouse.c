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
}

/*
 * PS/2 controller handshake helpers.
 *
 * Every command the controller accepts is answered with an acknowledge byte
 * on the data port. Those bytes have to be consumed, and the original code
 * consumed none of them, so the sequence was:
 *
 *   outb(0x64, 0x20)     ; ask for the config byte
 *   config = inb(0x60)   ; ...this reads the ACK (0xFA), not the config
 *   config |= 0x02
 *   outb(0x64, 0x60)
 *   outb(0x60, config)   ; writes 0xFA|0x02 back as the configuration
 *
 * which clobbered the aux-port bits and left IRQ 12 disabled -- the pointer
 * never moved. The two bytes left over from the 0xF6 and 0xF4 commands also
 * arrive as IRQ 12 data and get parsed as motion packets.
 */
static void ps2_wait_write(void) {
    /* Wait for the input buffer to drain (IBF clear).
     *
     * The bound is small on purpose. Under KVM every inb() is a VM exit, so a
     * long spin here is not slow, it is a boot that never finishes -- the same
     * failure the ATA wait loop had. The controller answers within a few
     * hundred microseconds or not at all, and either way the boot must
     * continue. */
    for (volatile int i = 0; i < 2000; i++) {
        if (!(inb(0x64) & 0x02)) return;
    }
}

static void ps2_drain_output(void) {
    /* Give the device a moment, then throw away whatever it queued. */
    for (volatile int i = 0; i < 200; i++) io_delay();
    if (inb(0x64) & 0x01) (void)inb(0x60);
}

static void ps2_write_command(u8 cmd) {
    ps2_wait_write();
    outb(0x64, cmd);
    ps2_drain_output();
}

static u8 ps2_write_data(u8 value) {
    ps2_wait_write();
    outb(0x60, value);
    for (volatile int i = 0; i < 200; i++) io_delay();
    if (inb(0x64) & 0x01) return inb(0x60);   /* the ACK, if any */
    return 0xFF;                              /* timed out */
}

void mouse_init(void) {
    /* Disable the aux port while it is reconfigured, or stray bytes arrive
     * mid-sequence and desynchronise the packet stream. */
    ps2_write_command(0xA7);

    /* Drain anything the controller was already holding. */
    ps2_drain_output();

    /* Enable the aux port. */
    ps2_write_command(0xA8);

    /* Read the configuration byte, skipping the ACK that answers 0x20. */
    u8 config;
    ps2_write_command(0x20);
    config = inb(0x60);
    config |= 0x02;        /* enable the aux interrupt (IRQ 12) */

    /* Write it back, skipping the ACK that answers 0x60. */
    ps2_write_command(0x60);
    (void)ps2_write_data(config);

    /* Defaults, then enable reporting. Each answers with an ACK. */
    (void)ps2_write_data(0xF6);
    (void)ps2_write_data(0xF4);

    /* Make sure IRQ 12 is unmasked in the PIC, in case anything masked it. */
    pic_unmask_irq(12);

    kprintf("Mouse: PS/2 enabled, config=0x%02x\n", config);
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
        overflow_seen = (byte & 0x40) || (byte & 0x20);
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
u32 g_mouse_packets = 0;

u32 mouse_packet_count(void) { return g_mouse_packets; }

mouse_state_t* mouse_get_state(void) { return &g_mouse; }
