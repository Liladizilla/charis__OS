#include <kernel/logo.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/vga.h>

#include <stdio.h>
#include <string.h>

framebuffer_t g_framebuffer;

static unsigned int fill_calls;
static unsigned int outline_calls;
static unsigned int text_calls;
static u32 first_x, first_y, first_w, first_h;
static int bounds_ok = 1;

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 colour) {
    (void)colour;
    if (fill_calls == 0) {
        first_x = x;
        first_y = y;
        first_w = w;
        first_h = h;
    }
    if (x > g_framebuffer.width || y > g_framebuffer.height ||
        w > g_framebuffer.width - x || h > g_framebuffer.height - y)
        bounds_ok = 0;
    fill_calls++;
}

void fb_draw_rect(u32 x, u32 y, u32 w, u32 h, u32 colour) {
    (void)x; (void)y; (void)w; (void)h; (void)colour;
    outline_calls++;
}

void gfx_draw_string_centered(u32 y, const char* value, u32 fg, u32 bg) {
    (void)y; (void)fg; (void)bg;
    if (value && *value) text_calls++;
}

void vga_puts(const char* value) { (void)value; }

static int check(int condition, const char* message) {
    if (condition) return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void) {
    int failures = 0;

    g_framebuffer.initialized = true;
    g_framebuffer.width = 1024;
    g_framebuffer.height = 768;
    logo_set_stage("TEST BOOT STAGE");
    logo_draw_gfx_centered();

    failures += check(fill_calls > 100, "emblem raster must produce visible pixel runs");
    failures += check(outline_calls == 1, "splash panel outline drawn exactly once");
    failures += check(text_calls == 3, "brand, tagline and real boot stage drawn");
    failures += check(bounds_ok, "all splash rectangles remain inside the framebuffer");
    failures += check(first_x == 348 && first_y == 193 &&
                      first_w == 328 && first_h == 382,
                      "1024x768 splash panel is centered and sized as expected");

    fill_calls = outline_calls = text_calls = 0;
    bounds_ok = 1;
    g_framebuffer.width = 640;
    g_framebuffer.height = 480;
    logo_draw_gfx_centered();
    failures += check(fill_calls > 100, "small framebuffer still renders the emblem");
    failures += check(bounds_ok, "small splash remains within the framebuffer");
    failures += check(first_x == 220 && first_y == 115 &&
                      first_w == 200 && first_h == 249,
                      "640x480 splash falls back to its compact scale");

    if (failures) return 1;
    puts("Logo rendering tests passed");
    return 0;
}
