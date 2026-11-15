/* display.c - validated primary display state.
 *
 * Until native display drivers and monitor discovery exist, the kernel exposes
 * only the framebuffer described by the Multiboot2 handoff and accepted by
 * fb_init(). This file deliberately does not invent resolutions, refresh
 * rates, monitor records or framebuffer addresses.
 */
#include <kernel/display.h>
#include <kernel/fb.h>
#include <kernel/printf.h>

display_state_t g_display = {0};

void display_init(void) {
    g_display.available = false;
    g_display.framebuffer_physical = 0;
    g_display.width = 0;
    g_display.height = 0;
    g_display.pitch = 0;
    g_display.bpp = 0;
    g_display.fb_type = 0;
    g_display.red_pos = 0;
    g_display.green_pos = 0;
    g_display.blue_pos = 0;

    if (!g_framebuffer.initialized) {
        kprintf("Display: no validated linear framebuffer; graphics display unavailable\n");
        return;
    }

    g_display.available = true;
    g_display.framebuffer_physical = g_framebuffer.phys_addr;
    g_display.width = g_framebuffer.width;
    g_display.height = g_framebuffer.height;
    g_display.pitch = g_framebuffer.pitch;
    g_display.bpp = g_framebuffer.bpp;
    g_display.fb_type = g_framebuffer.fb_type;
    g_display.red_pos = g_framebuffer.red_pos;
    g_display.green_pos = g_framebuffer.green_pos;
    g_display.blue_pos = g_framebuffer.blue_pos;

    kprintf("Display: active framebuffer %ux%u, %ubpp, pitch %u, RGB positions %u/%u/%u\n",
            g_display.width, g_display.height, g_display.bpp, g_display.pitch,
            g_display.red_pos, g_display.green_pos, g_display.blue_pos);
}
