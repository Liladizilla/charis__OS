#include <kernel/display.h>
#include <kernel/fb.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

monitor_t g_monitors[MAX_MONITORS];
int g_monitor_count = 0;

void display_init(void) {
    for (int i = 0; i < MAX_MONITORS; i++) {
        g_monitors[i].active = false;
    }
    g_monitor_count = 0;
    
    // Add primary monitor (VGA mode)
    g_monitors[0].width = 640;
    g_monitors[0].height = 480;
    g_monitors[0].bpp = 32;
    g_monitors[0].framebuffer = (u32*)0xA0000; // Fallback
    g_monitors[0].active = true;
    g_monitor_count = 1;
    
    vga_puts("Display: Primary monitor initialized (640x480)\n");
}

int display_add_monitor(int width, int height, int bpp, u32* fb) {
    if (g_monitor_count >= MAX_MONITORS) return -1;
    
    int idx = g_monitor_count;
    g_monitors[idx].width = width;
    g_monitors[idx].height = height;
    g_monitors[idx].bpp = bpp;
    g_monitors[idx].framebuffer = fb;
    g_monitors[idx].active = true;
    g_monitor_count++;
    
    kprintf("Display: Added monitor %d (%dx%d)\n", idx, width, height);
    return idx;
}

void display_set_primary(int index) {
    if (index >= 0 && index < g_monitor_count) {
        monitor_t tmp = g_monitors[0];
        g_monitors[0] = g_monitors[index];
        g_monitors[index] = tmp;
    }
}

int display_get_primary(void) {
    return 0;
}

void display_render_all(void) {
    // Composite all active monitors
    for (int i = 0; i < g_monitor_count; i++) {
        if (g_monitors[i].active) {
            kprintf("Display: Monitor %d ready\n", i);
        }
    }
}