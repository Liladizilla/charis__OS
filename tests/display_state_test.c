#include <kernel/display.h>
#include <kernel/fb.h>
#include <kernel/printf.h>

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
    failures += check(!g_display.available, "missing framebuffer must remain unavailable");
    failures += check(g_display.width == 0 && g_display.height == 0,
                      "missing framebuffer must not invent geometry");
    failures += check(g_display.framebuffer_physical == 0,
                      "missing framebuffer must not invent a physical address");

    g_framebuffer.initialized = true;
    g_framebuffer.phys_addr = 0xE0000000ULL;
    g_framebuffer.width = 1600;
    g_framebuffer.height = 900;
    g_framebuffer.pitch = 3200;
    g_framebuffer.bpp = 16;
    g_framebuffer.fb_type = 1;
    g_framebuffer.red_pos = 11;
    g_framebuffer.green_pos = 5;
    g_framebuffer.blue_pos = 0;
    display_init();

    failures += check(g_display.available, "validated framebuffer becomes available");
    failures += check(g_display.framebuffer_physical == 0xE0000000ULL,
                      "physical framebuffer address preserved");
    failures += check(g_display.width == 1600 && g_display.height == 900,
                      "real framebuffer dimensions preserved");
    failures += check(g_display.pitch == 3200 && g_display.bpp == 16,
                      "real pitch and pixel depth preserved");
    failures += check(g_display.fb_type == 1 &&
                      g_display.red_pos == 11 &&
                      g_display.green_pos == 5 &&
                      g_display.blue_pos == 0,
                      "pixel format metadata preserved");

    g_framebuffer.width = 1024;
    g_framebuffer.height = 768;
    g_framebuffer.pitch = 4096;
    g_framebuffer.bpp = 32;
    g_framebuffer.red_pos = 16;
    g_framebuffer.green_pos = 8;
    g_framebuffer.blue_pos = 0;
    display_init();
    failures += check(g_display.width == 1024 && g_display.height == 768,
                      "reinitialization publishes the current framebuffer mode");
    failures += check(g_display.bpp == 32 && g_display.pitch == 4096,
                      "reinitialization replaces old mode metadata");

    if (failures) return 1;
    puts("Display state tests passed");
    return 0;
}
