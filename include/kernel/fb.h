/* fb.h - Linear framebuffer driver
 *
 * The framebuffer is whatever the firmware advertised in the Multiboot2 GFX
 * tag, parsed by bootmem.c. Its physical address is usually above the 1GB
 * region the bootloader identity-maps, so the driver maps it into a kernel
 * window itself before drawing.
 */
#ifndef KERNEL_FB_H
#define KERNEL_FB_H

#include <kernel/types.h>

/* Kernel virtual window the framebuffer is mapped into. */
#define FB_VIRT_BASE  0xFFFFA00000000000ULL

typedef struct {
    u64   phys_addr;
    u32   pitch;          /* bytes per scanline, from the firmware */
    u32   width;
    u32   height;
    u8    bpp;
    u8    fb_type;        /* 0 indexed, 1 direct RGB, 2 EGA text */
    bool  initialized;

    /* Direct access for 32bpp only, which is what the rasteriser, the
     * compositor and the font blitter assume. NULL when the firmware picked a
     * different depth, so those callers fall back to fb_put_pixel(). */
    u32*  pixels;

    /* Bit position of each colour component within a pixel. */
    u8    red_pos;
    u8    green_pos;
    u8    blue_pos;

    /* Masks, derived from the positions and bit depth. */
    u32   red_mask;
    u32   green_mask;
    u32   blue_mask;
} framebuffer_t;

extern framebuffer_t g_framebuffer;

/* Map and set up the framebuffer described by the parsed boot info.
 * Returns 0 on success, negative if there is no usable framebuffer. */
int fb_init(void);

void fb_put_pixel(u32 x, u32 y, u32 argb);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 argb);
void fb_draw_rect(u32 x, u32 y, u32 w, u32 h, u32 argb);
void fb_clear(u32 argb);
void fb_swap(void);

/* Pixel format helpers: colours are always handled as 0xAARRGGBB. */
u32 fb_pack(u32 argb);
u32 fb_unpack(u32 native);

/* Historical name, kept because the window manager and desktop use it.
 * Call sites pass (r, g, b, a) -- note the order differs from FB_ARGB, which
 * takes alpha first, because that is how the original macro was defined.
 * New code should prefer FB_ARGB. */
#define FB_COLOR(r, g, b, a) FB_ARGB(a, r, g, b)

/* Palette of the desktop theme, as 0xAARRGGBB. */
#define FB_ARGB(a, r, g, b) ((((u32)(a) & 0xFF) << 24) | (((u32)(r) & 0xFF) << 16) | \
                             (((u32)(g) & 0xFF) << 8)  | ((u32)(b) & 0xFF))
#define FB_BLACK        FB_ARGB(0xFF, 0x00, 0x00, 0x00)
#define FB_WHITE        FB_ARGB(0xFF, 0xFF, 0xFF, 0xFF)
#define FB_RED          FB_ARGB(0xFF, 0xFF, 0x00, 0x00)
#define FB_GREEN        FB_ARGB(0xFF, 0x00, 0xFF, 0x00)
#define FB_BLUE         FB_ARGB(0xFF, 0x00, 0x00, 0xFF)
#define FB_YELLOW       FB_ARGB(0xFF, 0xFF, 0xFF, 0x00)
#define FB_CYAN         FB_ARGB(0xFF, 0x00, 0xFF, 0xFF)
#define FB_MAGENTA      FB_ARGB(0xFF, 0xFF, 0x00, 0xFF)
#define FB_GREY         FB_ARGB(0xFF, 0x80, 0x80, 0x80)
#define FB_DARK_GREY    FB_ARGB(0xFF, 0x20, 0x20, 0x20)
#define FB_TRANSPARENT  0x00000000

#endif
