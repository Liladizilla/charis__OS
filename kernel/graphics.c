#include <kernel/graphics.h>
#include <kernel/fb.h>
#include <kernel/psf.h>
#include <kernel/string.h>

graphics_t g_graphics = {0};

void graphics_init(void) {
    g_graphics.fb = &g_framebuffer;
    g_graphics.color = FB_WHITE;
    g_graphics.bg_color = FB_BLACK;
}

u32 graphics_rgb(u8 r, u8 g, u8 b) {
    return ((0xFF) << 24) | ((r) << 16) | ((g) << 8) | (b);
}

void graphics_set_color(u32 color) {
    g_graphics.color = color;
}

void graphics_set_bg(u32 color) {
    g_graphics.bg_color = color;
}

void graphics_put_pixel(u32 x, u32 y) {
    fb_put_pixel(x, y, g_graphics.color);
}

// Bresenham's line algorithm
void graphics_line(u32 x0, u32 y0, u32 x1, u32 y1) {
    s32 dx = (s32)x1 - (s32)x0;
    s32 dy = (s32)y1 - (s32)y0;
    s32 sx = dx > 0 ? 1 : -1;
    s32 sy = dy > 0 ? 1 : -1;
    s32 err = dx > dy ? dx : -dy;
    
    s32 x = (s32)x0;
    s32 y = (s32)y0;
    
    if (g_framebuffer.initialized) {
        fb_put_pixel(x0, y0, g_graphics.color);
    }
    
    while (x != (s32)x1 || y != (s32)y1) {
        s32 e2 = 2 * err;
        if (e2 >= dy) {
            err -= dy;
            x += sx;
        }
        if (e2 <= dx) {
            err -= dx;
            y += sy;
        }
        if (g_framebuffer.initialized) {
            fb_put_pixel((u32)x, (u32)y, g_graphics.color);
        }
    }
}

// Rectangle filling using horizontal lines
void graphics_rect(u32 x, u32 y, u32 w, u32 h, bool filled) {
    if (filled) {
        for (u32 py = y; py < y + h; py++) {
            for (u32 px = x; px < x + w; px++) {
                fb_put_pixel(px, py, g_graphics.color);
            }
        }
    } else {
        graphics_line(x, y, x + w - 1, y);
        graphics_line(x, y, x, y + h - 1);
        graphics_line(x + w - 1, y, x + w - 1, y + h - 1);
        graphics_line(x, y + h - 1, x + w - 1, y + h - 1);
    }
}

// Midpoint circle algorithm
void graphics_circle(u32 cx, u32 cy, u32 r, bool filled) {
    s32 x = 0;
    s32 y = (s32)r;
    s32 d = 1 - (s32)r;
    s32 dx = 0;
    s32 dy = 0;
    
    if (!filled) {
        for (;;) {
            fb_put_pixel(cx + x, cy + y, g_graphics.color);
            fb_put_pixel(cx + x, cy - y, g_graphics.color);
            fb_put_pixel(cx - x, cy + y, g_graphics.color);
            fb_put_pixel(cx - x, cy - y, g_graphics.color);
            fb_put_pixel(cx + y, cy + x, g_graphics.color);
            fb_put_pixel(cx + y, cy - x, g_graphics.color);
            fb_put_pixel(cx - y, cy + x, g_graphics.color);
            fb_put_pixel(cx - y, cy - x, g_graphics.color);
            
            if (x >= y) break;
            
            x++;
            if (d < 0) {
                d += 2 * x + 1;
            } else {
                y--;
                d += 2 * (x - y) + 1;
            }
        }
    } else {
        for (s32 fy = -(s32)r; fy <= (s32)r; fy++) {
            for (s32 fx = -(s32)r; fx <= (s32)r; fx++) {
                if (fx * fx + fy * fy <= (s32)(r * r)) {
                    fb_put_pixel(cx + fx, cy + fy, g_graphics.color);
                }
            }
        }
    }
}

void graphics_put_char(u32 x, u32 y, char c) {
    psf_draw_char(x, y, c, g_graphics.color, g_graphics.bg_color);
}

void graphics_put_string(u32 x, u32 y, const char* str) {
    psf_draw_string(x, y, str, g_graphics.color, g_graphics.bg_color);
}