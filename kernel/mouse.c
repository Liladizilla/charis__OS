#include <kernel/mouse.h>
#include <kernel/io.h>
#include <kernel/vga.h>

mouse_state_t g_mouse = {50, 50, 0, 0, 0};
static uint32_t mouse_screen_width = 800;
static uint32_t mouse_screen_height = 600;

void mouse_set_bounds(uint32_t width, uint32_t height) {
    mouse_screen_width = width;
    mouse_screen_height = height;
}

void mouse_init(void) {
    /* Enable mouse in PS/2 controller (port 0x64) */
    outb(0x64, 0xA8); /* Enable mouse port */
    io_delay();
    
    /* Configure mouse (port 0x60) */
    outb(0x64, 0x20); /* Command to read config byte */
    io_delay();
    u8 config = inb(0x60);
    config |= 0x02; /* Enable IRQ 12 */
    outb(0x64, 0x60); /* Write config byte */
    io_delay();
    outb(0x60, config);
    io_delay();
    
    /* Send mouse initialization commands */
    outb(0x60, 0xF6); /* Set defaults */
    io_delay();
    outb(0x60, 0xF4); /* Enable data reporting */
    
    vga_puts("Mouse initialized\n");
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