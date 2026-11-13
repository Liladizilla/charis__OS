/* cursor.c - flicker-free software pointer compositing */
#include <kernel/cursor.h>
#include <kernel/fb.h>
#include <kernel/mouse.h>
#ifdef CURSOR_TRACE
#include <kernel/serial.h>
#endif

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

/* Framebuffer surfaces can be repainted independently of the pointer. The
 * save-under is then still marked valid even though its pixels are gone.
 * Check the painted shape before taking the same-position fast path. */
static bool cursor_color_matches(u32 actual, u32 expected) {
    for (u32 shift = 0; shift <= 16u; shift += 8u) {
        u32 a = (actual >> shift) & 0xFFu;
        u32 e = (expected >> shift) & 0xFFu;
        if (a > e ? a - e > 8u : e - a > 8u) return false;
    }
    return true;
}

static bool cursor_overlay_intact(void) {
    const u32 fill = FB_ARGB(0xFF, 0xF4, 0xF6, 0xF8);
    const u32 outline = FB_ARGB(0xFF, 0x08, 0x0B, 0x10);

    for (u32 y = 0; y < cursor_saved_height; y++) {
        for (u32 x = 0; x < cursor_saved_width; x++) {
            u32 expected = 0;
            if (cursor_shape[y][x] == 'X') {
                expected = fill;
            } else if ((y + 1u < CURSOR_HEIGHT &&
                        cursor_shape[y + 1u][x] == 'X') ||
                       (x + 1u < CURSOR_WIDTH &&
                        cursor_shape[y][x + 1u] == 'X')) {
                expected = outline;
            } else {
                continue;
            }

            u32 actual = fb_get_pixel((u32)cursor_saved_x + x,
                                      (u32)cursor_saved_y + y);
            if (!cursor_color_matches(actual, expected)) {
#ifdef CURSOR_TRACE
                static u32 cursor_mismatch_logs = 0;
                if (cursor_mismatch_logs < 24u) {
                    cursor_mismatch_logs++;
                    serial_printf("CURSOR_TRACE mismatch at=%d,%d offset=%u,%u actual=%08x expected=%08x bpp=%u\n",
                                  cursor_saved_x, cursor_saved_y, x, y,
                                  actual, expected, g_framebuffer.bpp);
                }
#endif
                return false;
            }
        }
    }
    return true;
}

/* Remove only cursor-colored pixels that survived a partial surface repaint.
 * Pixels already replaced by the surface are left untouched. */
static void cursor_remove_stale_pixels(void) {
    const u32 fill = FB_ARGB(0xFF, 0xF4, 0xF6, 0xF8);
    const u32 outline = FB_ARGB(0xFF, 0x08, 0x0B, 0x10);
    fb_reset_clip();

    for (u32 y = 0; y < cursor_saved_height; y++) {
        for (u32 x = 0; x < cursor_saved_width; x++) {
            u32 expected = 0;
            if (cursor_shape[y][x] == 'X') {
                expected = fill;
            } else if ((y + 1u < CURSOR_HEIGHT &&
                        cursor_shape[y + 1u][x] == 'X') ||
                       (x + 1u < CURSOR_WIDTH &&
                        cursor_shape[y][x + 1u] == 'X')) {
                expected = outline;
            } else {
                continue;
            }

            u32 px = (u32)cursor_saved_x + x;
            u32 py = (u32)cursor_saved_y + y;
            if (cursor_color_matches(fb_get_pixel(px, py), expected))
                fb_put_pixel(px, py, cursor_under[y][x]);
        }
    }
}

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
        mouse.y == cursor_saved_y) {
        if (cursor_overlay_intact()) return;

        /* A surface repaint removed or partially covered the pointer without
         * going through cursor_erase(). Reconcile surviving cursor pixels and
         * take a fresh save-under from the newly painted desktop. */
        cursor_remove_stale_pixels();
        cursor_saved_valid = false;
    }

#ifdef CURSOR_TRACE
    serial_printf("CURSOR_TRACE draw x=%d y=%d old=%d,%d valid=%u\n",
                  mouse.x, mouse.y, cursor_saved_x, cursor_saved_y,
                  cursor_saved_valid ? 1u : 0u);
#endif
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
