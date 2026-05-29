#include <kernel/mouse.h>
#include <kernel/io.h>
#include <kernel/vga.h>

mouse_state_t g_mouse = {50, 50, false, false, false};

// Mouse data buffer (3 bytes per packet)
static u8 mouse_packet[3];
static int mouse_byte_index = 0;

void mouse_init(void) {
    // Enable mouse in PS/2 controller (port 0x64)
    outb(0x64, 0xA8); // Enable mouse port
    io_delay();
    
    // Configure mouse (port 0x60)
    outb(0x64, 0x20); // Command to read config byte
    io_delay();
    u8 config = inb(0x60);
    config |= 0x02; // Enable IRQ 12
    outb(0x64, 0x60); // Write config byte
    io_delay();
    outb(0x60, config);
    io_delay();
    
    // Send mouse initialization commands
    outb(0x60, 0xF6); // Set defaults
    io_delay();
    outb(0x60, 0xF4); // Enable data reporting
    
    vga_puts("Mouse initialized\n");
}

void mouse_handler(void) {
    u8 byte = inb(0x60);
    
    mouse_packet[mouse_byte_index++] = byte;
    if (mouse_byte_index < 3) {
        return; // Wait for full packet
    }
    mouse_byte_index = 0;
    
    // Parse packet: [status, dx, dy]
    bool left = mouse_packet[0] & 0x01;
    bool right = mouse_packet[0] & 0x02;
    bool middle = mouse_packet[0] & 0x04;
    
    int dx = (s8)mouse_packet[1]; // Signed
    int dy = (s8)mouse_packet[2]; // Signed (inverted Y)
    
    g_mouse.left_pressed = left;
    g_mouse.right_pressed = right;
    g_mouse.middle_pressed = middle;
    g_mouse.x += dx;
    g_mouse.y -= dy; // Invert Y for screen coordinates
    
    // Clamp to screen bounds (80x25 text mode = 640x500 approx)
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
}

bool mouse_has_input(void) {
    return mouse_byte_index > 0;
}

int mouse_get_movement(int* dx, int* dy) {
    // This would need better state tracking
    return 0;
}

int mouse_get_buttons(int* buttons) {
    if (buttons) {
        *buttons = (g_mouse.left_pressed ? 1 : 0) |
                   (g_mouse.right_pressed ? 2 : 0) |
                   (g_mouse.middle_pressed ? 4 : 0);
    }
    return 0;
}