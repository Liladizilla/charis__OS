/* graphics.h - 2D drawing primitives for CharisOS */
#pragma once
#include <kernel/types.h>
#include <kernel/fb.h>

// 2D graphics context
typedef struct {
    framebuffer_t* fb;
    u32 color;
    u32 bg_color;
} graphics_t;

extern graphics_t g_graphics;

// Initialize graphics subsystem
void graphics_init(void);

// Drawing primitives
void graphics_put_pixel(u32 x, u32 y);
void graphics_line(u32 x0, u32 y0, u32 x1, u32 y1);
void graphics_rect(u32 x, u32 y, u32 w, u32 h, bool filled);
void graphics_circle(u32 cx, u32 cy, u32 r, bool filled);

// Text rendering
void graphics_put_char(u32 x, u32 y, char c);
void graphics_put_string(u32 x, u32 y, const char* str);

// Color helpers
u32 graphics_rgb(u8 r, u8 g, u8 b);
void graphics_set_color(u32 color);
void graphics_set_bg(u32 color);