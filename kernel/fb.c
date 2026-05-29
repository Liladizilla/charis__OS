#include <kernel/fb.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/memory.h>

framebuffer_t g_framebuffer = {0};

int fb_init(multiboot_info_t* mbi) {
    (void)mbi;
    
    // Multiboot2 tag parsing would go here
    // For now, just initialize with VGA text mode as fallback
    
    vga_puts("Framebuffer not yet initialized (waiting for Multiboot2 GFX tag)\n");
    return -1;
}

void fb_put_pixel(u32 x, u32 y, u32 color) {
    if (!g_framebuffer.initialized) return;
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return;
    
    g_framebuffer.pixels[y * (g_framebuffer.pitch / 4) + x] = color;
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!g_framebuffer.initialized) return;
    
    for (u32 py = y; py < y + h && py < g_framebuffer.height; py++) {
        for (u32 px = x; px < x + w && px < g_framebuffer.width; px++) {
            fb_put_pixel(px, py, color);
        }
    }
}

void fb_clear(u32 color) {
    if (!g_framebuffer.initialized) return;
    
    for (u32 i = 0; i < g_framebuffer.width * g_framebuffer.height; i++) {
        g_framebuffer.pixels[i] = color;
    }
}

void fb_swap(void) {
    // No-op for single-buffer mode (would copy backbuffer to frontbuffer)
}