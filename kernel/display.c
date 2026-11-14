/* display.c - truthful primary display state.
 *
 * The only display currently exposed by the kernel is the linear framebuffer
 * handed over by the bootloader and validated by fb_init(). This module does
 * not invent monitor modes, claim multi-monitor support, or point graphics at
 * the legacy VGA aperture (0xA0000 is not a usable linear framebuffer here).
 */
#include <kernel/display.h>
#include <kernel/fb.h>
#include <kernel/printf.h>

monitor_t g_monitors[MAX_MONITORS];
int g_monitor_count = 0;

void display_init(void) {
    for (int i = 0; i < MAX_MONITORS; i++) {
        g_monitors[i].width = 0;
        g_monitors[i].height = 0;
        g_monitors[i].bpp = 0;
        g_monitors[i].framebuffer = 0;
        g_monitors[i].active = false;
    }
    g_monitor_count = 0;

    if (!g_framebuffer.initialized) {
        kprintf("Display: no linear framebuffer; graphics display unavailable\n");
        return;
    }

    /* framebuffer is a direct u32* only for 32bpp. At 16/24bpp the drawing
     * API remains fb_put_pixel(), which honors the firmware's pixel format
     * and pitch instead of exposing an incorrectly typed pointer. */
    g_monitors[0].width = (int)g_framebuffer.width;
    g_monitors[0].height = (int)g_framebuffer.height;
    g_monitors[0].bpp = (int)g_framebuffer.bpp;
    g_monitors[0].framebuffer = g_framebuffer.pixels;
    g_monitors[0].active = true;
    g_monitor_count = 1;

    kprintf("Display: primary framebuffer %ux%u, %ubpp, pitch %u\n",
            g_framebuffer.width, g_framebuffer.height,
            g_framebuffer.bpp, g_framebuffer.pitch);
}

int display_add_monitor(int width, int height, int bpp, u32* fb) {
    if (g_monitor_count >= MAX_MONITORS ||
        width <= 0 || height <= 0 ||
        (bpp != 16 && bpp != 24 && bpp != 32) || !fb)
        return -1;

    int idx = g_monitor_count;
    g_monitors[idx].width = width;
    g_monitors[idx].height = height;
    g_monitors[idx].bpp = bpp;
    g_monitors[idx].framebuffer = fb;
    g_monitors[idx].active = true;
    g_monitor_count++;

    kprintf("Display: registered framebuffer %d (%dx%d, %dbpp)\n",
            idx, width, height, bpp);
    return idx;
}

void display_set_primary(int index) {
    if (index < 0 || index >= g_monitor_count || index == 0) return;

    monitor_t tmp = g_monitors[0];
    g_monitors[0] = g_monitors[index];
    g_monitors[index] = tmp;
}

int display_get_primary(void) {
    return g_monitor_count > 0 ? 0 : -1;
}

void display_render_all(void) {
    /* Diagnostic inventory only. Independent scanout and a multi-monitor
     * compositor are not implemented yet. */
    for (int i = 0; i < g_monitor_count; i++) {
        if (g_monitors[i].active) {
            kprintf("Display: framebuffer %d registered (%dx%d, %dbpp)\n",
                    i, g_monitors[i].width, g_monitors[i].height,
                    g_monitors[i].bpp);
        }
    }
}
