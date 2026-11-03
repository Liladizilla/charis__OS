/* display.c - Firmware-backed display inventory.
 *
 * This stage exposes the framebuffer mode handed to us by Multiboot. It does
 * not invent a VGA monitor or claim multi-head support without a driver.
 */
#include <kernel/display.h>
#include <kernel/fb.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

monitor_t g_monitors[MAX_MONITORS];
int g_monitor_count = 0;

void display_init(void) {
    for (int i = 0; i < MAX_MONITORS; i++) {
        g_monitors[i].width = 0;
        g_monitors[i].height = 0;
        g_monitors[i].bpp = 0;
        g_monitors[i].framebuffer = NULL;
        g_monitors[i].active = false;
    }
    g_monitor_count = 0;

    if (!g_framebuffer.initialized) {
        vga_puts("Display: No linear framebuffer; text console remains active\n");
        return;
    }

    g_monitors[0].width = (int)g_framebuffer.width;
    g_monitors[0].height = (int)g_framebuffer.height;
    g_monitors[0].bpp = (int)g_framebuffer.bpp;
    g_monitors[0].framebuffer = g_framebuffer.pixels;
    g_monitors[0].active = true;
    g_monitor_count = 1;

    kprintf("Display: Firmware framebuffer %ux%u, %u bpp, pitch %u bytes\n",
            g_framebuffer.width, g_framebuffer.height,
            (u32)g_framebuffer.bpp, g_framebuffer.pitch);
}

int display_add_monitor(int width, int height, int bpp, u32* fb) {
    if (g_monitor_count >= MAX_MONITORS || width <= 0 || height <= 0 ||
        (bpp != 16 && bpp != 24 && bpp != 32) || !fb) return -1;

    int idx = g_monitor_count;
    g_monitors[idx].width = width;
    g_monitors[idx].height = height;
    g_monitors[idx].bpp = bpp;
    g_monitors[idx].framebuffer = fb;
    g_monitors[idx].active = true;
    g_monitor_count++;

    kprintf("Display: Driver registered monitor %d (%dx%d, %d bpp)\n",
            idx, width, height, bpp);
    return idx;
}

void display_set_primary(int index) {
    if (index < 0 || index >= g_monitor_count || !g_monitors[index].active) return;
    monitor_t tmp = g_monitors[0];
    g_monitors[0] = g_monitors[index];
    g_monitors[index] = tmp;
}

int display_get_primary(void) {
    return (g_monitor_count > 0 && g_monitors[0].active) ? 0 : -1;
}

void display_render_all(void) {
    /* Rendering remains owned by the single active framebuffer compositor. */
}
