/* fb.h - Framebuffer driver for graphics mode */
#pragma once
#include <kernel/types.h>
#include <kernel/multiboot.h>

typedef struct {
    u64 addr;
    u32 pitch;
    u32 width;
    u32 height;
    u32 bpp;
    u32* pixels;
    bool initialized;
} framebuffer_t;

extern framebuffer_t g_framebuffer;

// Initialize framebuffer from Multiboot2 tag
int fb_init(multiboot_info_t* mbi);

// Drawing primitives
void fb_put_pixel(u32 x, u32 y, u32 color);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_clear(u32 color);
void fb_swap(void); // Double buffering

// Colors (ARGB format for 32-bit)
#define FB_COLOR(r, g, b, a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define FB_BLACK    0xFF000000
#define FB_WHITE    0xFFFFFFFF
#define FB_RED      0xFFFF0000
#define FB_GREEN    0xFF00FF00
#define FB_BLUE     0xFF0000FF