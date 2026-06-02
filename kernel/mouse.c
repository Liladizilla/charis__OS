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
    /* Mouse data buffer (3 bytes per packet) */
    static u8 mouse_packet[3];
    static int mouse_byte_index = 0;
    
    u8 byte = inb(0x60);
    
    mouse_packet[mouse_byte_index++] = byte;
    if (mouse_byte_index < 3) {
        return; /* Wait for full packet */
    }
    mouse_byte_index = 0;
    
    /* Parse packet:
     * Byte 0: [Y overflow][X overflow][Y sign][X sign][1][Middle btn][Right btn][Left btn]
     * Byte 1: X movement delta (signed)
     * Byte 2: Y movement delta (signed, Y axis inverted)
     */
    u8 btn_state = mouse_packet[0];
    g_mouse.buttons = (btn_state & 0x01) |       /* Left button */
                    ((btn_state & 0x02) << 0) |  /* Right button */
                    ((btn_state & 0x04) << 1);   /* Middle button */
    
    /* Get signed deltas with 9-bit extension */
    s8 dx_raw = (s8)mouse_packet[1];
    s8 dy_raw = (s8)mouse_packet[2];
    
    /* Store deltas */
    g_mouse.dx = (s32)dx_raw;
    g_mouse.dy = (s32)dy_raw;
    
    /* Update position and clamp to screen bounds */
    g_mouse.x += g_mouse.dx;
    g_mouse.y -= g_mouse.dy; /* Invert Y for screen coordinates */
    
    /* Clamp to screen bounds */
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= (int32_t)mouse_screen_width) g_mouse.x = mouse_screen_width - 1;
    if (g_mouse.y >= (int32_t)mouse_screen_height) g_mouse.y = mouse_screen_height - 1;
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