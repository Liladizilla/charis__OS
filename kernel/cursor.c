/* cursor.c - flicker-free software pointer compositing */
#include <kernel/cursor.h>
#include <kernel/fb.h>
#include <kernel/mouse.h>

#define CURSOR_WIDTH  10u
#define CURSOR_HEIGHT 16u

static const char cursor_shape[CURSOR_HEIGHT][CURSOR_WIDTH + 1u] = {
    "X.........",
    "XX........",
    "X.X.......",
    "X..X......",
    "X...X.....",
    "X....X....",
    "X.....X...",
    "X......X..",
    "X.......X.",
    "X........X",
    "X.....XXXXX",
    "X....X.....",
    "X...X......",
    "X..X.......",
    "X.X........",
    "XX........."
};

static u32 cursor_under[CURSOR_HEIGHT][CURSOR_WIDTH];
static s32 cursor_saved_x;
static s32 cursor_saved_y;
static u32 cursor_saved_width;
static u32 cursor_saved_height;
static bool cursor_saved_valid;

void cursor_erase(void) {
    if (!cursor_saved_valid) return;
    if (!g_framebuffer.initialized) {
        cursor_saved_valid = false;
        return;
    }

    /* A cursor restore is a tiny independent compositor pass. Never inherit
     * a window's clip rectangle, or the old pointer can leave visible trails. */
    fb_reset_clip();
    for (u32 y = 0; y < cursor_saved_height; y++) {
        for (u32 x = 0; x < cursor_saved_width; x++) {
            fb_put_pixel((u32)cursor_saved_x + x, (u32)cursor_saved_y + y,
                         cursor_under[y][x]);
        }
    }
    cursor_saved_valid = false;
}

void cursor_draw(void) {
    if (!g_framebuffer.initialized) {
        cursor_erase();
        return;
    }

    mouse_state_t mouse;
    mouse_get_snapshot(&mouse);
    if (mouse.x < 0 || mouse.y < 0 ||
        (u32)mouse.x >= g_framebuffer.width ||
        (u32)mouse.y >= g_framebuffer.height) {
        cursor_erase();
        return;
    }

    if (cursor_saved_valid && mouse.x == cursor_saved_x &&
        mouse.y == cursor_saved_y)
        return;

    cursor_erase();
    fb_reset_clip();

    u32 available_width = g_framebuffer.width - (u32)mouse.x;
    u32 available_height = g_framebuffer.height - (u32)mouse.y;
    u32 draw_width = available_width < CURSOR_WIDTH ?
                     available_width : CURSOR_WIDTH;
    u32 draw_height = available_height < CURSOR_HEIGHT ?
                      available_height : CURSOR_HEIGHT;

    cursor_saved_x = mouse.x;
    cursor_saved_y = mouse.y;
    cursor_saved_width = draw_width;
    cursor_saved_height = draw_height;

    for (u32 y = 0; y < draw_height; y++) {
        for (u32 x = 0; x < draw_width; x++) {
            cursor_under[y][x] = fb_get_pixel((u32)mouse.x + x,
                                               (u32)mouse.y + y);
        }
    }
    cursor_saved_valid = true;

    const u32 fill = FB_ARGB(0xFF, 0xF4, 0xF6, 0xF8);
    const u32 outline = FB_ARGB(0xFF, 0x08, 0x0B, 0x10);
    for (u32 y = 0; y < draw_height; y++) {
        for (u32 x = 0; x < draw_width; x++) {
            if (cursor_shape[y][x] == 'X') {
                fb_put_pixel((u32)mouse.x + x, (u32)mouse.y + y, fill);
                continue;
            }
            bool edge = (y + 1u < CURSOR_HEIGHT &&
                         cursor_shape[y + 1u][x] == 'X') ||
                        (x + 1u < CURSOR_WIDTH &&
                         cursor_shape[y][x + 1u] == 'X');
            if (edge)
                fb_put_pixel((u32)mouse.x + x, (u32)mouse.y + y, outline);
        }
    }
}
