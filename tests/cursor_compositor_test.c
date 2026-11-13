#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/cursor.h>
#include <kernel/fb.h>
#include <kernel/mouse.h>

#define WIDTH 640u
#define HEIGHT 480u

framebuffer_t g_framebuffer = {
    .width = WIDTH,
    .height = HEIGHT,
    .initialized = true
};

static u32 pixels[WIDTH * HEIGHT];
static bool clip_enabled;
static u32 clip_x, clip_y, clip_w, clip_h;
static mouse_state_t test_mouse;

void mouse_get_snapshot(mouse_state_t* out) {
    if (out) *out = test_mouse;
}

void fb_reset_clip(void) {
    clip_enabled = false;
    clip_x = clip_y = clip_w = clip_h = 0;
}

u32 fb_get_pixel(u32 x, u32 y) {
    if (x >= WIDTH || y >= HEIGHT) return 0;
    return pixels[(usize)y * WIDTH + x];
}

void fb_put_pixel(u32 x, u32 y, u32 color) {
    if (x >= WIDTH || y >= HEIGHT) return;
    if (clip_enabled && (x < clip_x || y < clip_y ||
        x - clip_x >= clip_w || y - clip_y >= clip_h)) return;
    pixels[(usize)y * WIDTH + x] = color;
}

static void expect(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void fill_pattern(void) {
    for (u32 y = 0; y < HEIGHT; y++)
        for (u32 x = 0; x < WIDTH; x++)
            pixels[(usize)y * WIDTH + x] =
                0xFF000000u | ((x & 0xFFu) << 16) |
                ((y & 0xFFu) << 8) | ((x + y) & 0xFFu);
}

static bool rect_matches(const u32* before, u32 x, u32 y, u32 w, u32 h) {
    for (u32 row = 0; row < h; row++)
        for (u32 col = 0; col < w; col++)
            if (pixels[(usize)(y + row) * WIDTH + x + col] !=
                before[(usize)(y + row) * WIDTH + x + col])
                return false;
    return true;
}

int main(void) {
    static u32 before[WIDTH * HEIGHT];
    fill_pattern();
    memcpy(before, pixels, sizeof(pixels));

    test_mouse.x = 20;
    test_mouse.y = 24;
    cursor_draw();
    expect(pixels[24u * WIDTH + 20u] != before[24u * WIDTH + 20u],
           "cursor pixels are painted");
    cursor_erase();
    expect(rect_matches(before, 20, 24, 10, 16),
           "erase restores every underlying pixel exactly");

    /* Simulate a window renderer leaving a restrictive clip active. Cursor
     * compositing must reset that clip for both drawing and save-under restore. */
    clip_enabled = true;
    clip_x = 300; clip_y = 300; clip_w = 20; clip_h = 20;
    test_mouse.x = 40;
    test_mouse.y = 50;
    cursor_draw();
    expect(pixels[50u * WIDTH + 40u] != before[50u * WIDTH + 40u],
           "cursor drawing clears an inherited framebuffer clip");
    clip_enabled = true;
    clip_x = 300; clip_y = 300; clip_w = 20; clip_h = 20;
    cursor_erase();
    expect(rect_matches(before, 40, 50, 10, 16),
           "cursor restoration clears an inherited framebuffer clip");

    fill_pattern();
    memcpy(before, pixels, sizeof(pixels));
    test_mouse.x = 30;
    test_mouse.y = 40;
    cursor_draw();
    test_mouse.x = 80;
    test_mouse.y = 90;
    cursor_draw();
    expect(rect_matches(before, 30, 40, 10, 16),
           "moving the cursor restores its previous footprint");
    expect(pixels[90u * WIDTH + 80u] != before[90u * WIDTH + 80u],
           "new cursor position is painted");

    cursor_erase();
    fill_pattern();
    memcpy(before, pixels, sizeof(pixels));
    cursor_draw();
    cursor_erase();
    expect(rect_matches(before, 80, 90, 10, 16),
           "a desktop repaint can safely reuse the same cursor position");

    test_mouse.x = (int)WIDTH - 2;
    test_mouse.y = (int)HEIGHT - 2;
    cursor_draw();
    cursor_erase();
    expect(true, "cursor footprint is clipped at the lower-right display edge");

    puts("cursor save-under compositor tests: PASS");
    return 0;
}
