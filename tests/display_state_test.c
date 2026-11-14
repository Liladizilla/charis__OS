#include <kernel/display.h>
#include <kernel/fb.h>
#include <kernel/printf.h>

#include <stdarg.h>
#include <stdio.h>

framebuffer_t g_framebuffer;

void kprintf(const char* format, ...) {
    (void)format;
}

static int check(int condition, const char* message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    int failures = 0;

    g_framebuffer.initialized = false;
    display_init();
    failures += check(g_monitor_count == 0, "no framebuffer must not invent a monitor");
    failures += check(display_get_primary() == -1, "missing primary must return -1");
    failures += check(!g_monitors[0].active, "empty display inventory must be inactive");

    g_framebuffer.initialized = true;
    g_framebuffer.width = 1600;
    g_framebuffer.height = 900;
    g_framebuffer.bpp = 16;
    g_framebuffer.pitch = 3200;
    g_framebuffer.pixels = 0;
    display_init();
    failures += check(g_monitor_count == 1, "validated framebuffer becomes primary");
    failures += check(display_get_primary() == 0, "primary index is zero");
    failures += check(g_monitors[0].width == 1600 && g_monitors[0].height == 900,
                      "primary dimensions match firmware framebuffer");
    failures += check(g_monitors[0].bpp == 16, "primary bit depth matches firmware");
    failures += check(g_monitors[0].framebuffer == 0,
                      "16bpp framebuffer is not exposed as a u32 pointer");

    static u32 secondary_pixels[16];
    failures += check(display_add_monitor(1280, 720, 32, secondary_pixels) == 1,
                      "valid registered framebuffer gets next slot");
    failures += check(display_add_monitor(0, 720, 32, secondary_pixels) == -1,
                      "zero-width display rejected");
    failures += check(display_add_monitor(800, 600, 8, secondary_pixels) == -1,
                      "unsupported indexed depth rejected");
    failures += check(display_add_monitor(800, 600, 32, 0) == -1,
                      "missing framebuffer pointer rejected");
    failures += check(g_monitor_count == 2, "invalid displays do not modify inventory");

    display_set_primary(1);
    failures += check(g_monitors[0].width == 1280 && g_monitors[1].width == 1600,
                      "primary selection swaps registered display records");

    if (failures) return 1;
    puts("Display state tests passed");
    return 0;
}
