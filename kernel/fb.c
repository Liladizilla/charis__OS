/* fb.c - Linear framebuffer driver
 *
 * The boot code identity-maps only the first gigabyte with 2MB pages, and
 * firmware framebuffers are usually well above that (QEMU places the standard
 * VGA window at 0xFD000000). So the driver maps the advertised physical range
 * into a kernel window with ordinary 4KB pages before anything draws to it.
 *
 * Pixel format is taken from the GFX tag rather than assumed: the tag reports
 * the bit position of each colour component, and not every firmware uses the
 * usual BGRX layout. Drawing code works in 0xAARRGGBB and this file converts.
 */

#include <kernel/fb.h>
#include <kernel/bootmem.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/printf.h>
#include <kernel/string.h>

framebuffer_t g_framebuffer = {0};

/* Build a mask of `bits` bits positioned at `pos`. */
static u32 mask_at(u8 pos, u8 bits) {
    if (bits == 0 || bits >= 32) return 0;
    return (((u32)1 << bits) - 1) << pos;
}

/* Component width in bits, inferred from the mask. */
static u8 mask_bits(u32 mask) {
    u8 n = 0;
    while (mask) { n += (u32)(mask & 1); mask >>= 1; }
    return n;
}

/* Scale an 8-bit channel onto a narrower component without overflow. */
static u32 scale_channel(u8 value, u8 out_bits) {
    if (out_bits == 0) return 0;
    if (out_bits >= 8) return (u32)value << (out_bits - 8);
    /* Replicate the high bits into the low ones so full-scale input maps to
     * full-scale output: 0xFF -> 0x1F rather than saturating early. */
    return (u32)((value * (((u32)1 << out_bits) - 1) + 127) / 255);
}

u32 fb_pack(u32 argb) {
    framebuffer_t* fb = &g_framebuffer;
    if (!fb->initialized) return 0;

    /* Each channel has to be moved into its bit position before masking.
     * ANDing the plain 8-bit value against a mask that sits at, say, bit 16
     * would just produce zero. */
    u32 r = scale_channel((u8)(argb >> 16), mask_bits(fb->red_mask));
    u32 g = scale_channel((u8)(argb >> 8),  mask_bits(fb->green_mask));
    u32 b = scale_channel((u8)argb,        mask_bits(fb->blue_mask));

    u32 out = ((r << fb->red_pos)   & fb->red_mask)
            | ((g << fb->green_pos) & fb->green_mask)
            | ((b << fb->blue_pos)  & fb->blue_mask);
    return out;
}

u32 fb_unpack(u32 native) {
    framebuffer_t* fb = &g_framebuffer;
    if (!fb->initialized) return 0;

    u8 rb = mask_bits(fb->red_mask), gb = mask_bits(fb->green_mask), bb = mask_bits(fb->blue_mask);
    u32 r = rb ? (((native & fb->red_mask)   >> fb->red_pos)   * 255) / ((1u << rb) - 1) : 0;
    u32 g = gb ? (((native & fb->green_mask) >> fb->green_pos) * 255) / ((1u << gb) - 1) : 0;
    u32 b = bb ? (((native & fb->blue_mask)  >> fb->blue_pos)  * 255) / ((1u << bb) - 1) : 0;
    return FB_ARGB(0xFF, r, g, b);
}

int fb_init(void) {
    framebuffer_t* fb = &g_framebuffer;

    /* CHARIS_TEXT_ONLY builds a console-only image. Useful on hardware with no
     * linear framebuffer, or where reserving several megabytes for a desktop
     * the user cannot see is not worth it. */
#ifdef CHARIS_TEXT_ONLY
    kprintf("FB: text-only build, graphics disabled\n");
    return -1;
#endif

    if (!boot_info.has_framebuffer) {
        kprintf("FB: firmware provided no framebuffer\n");
        return -1;
    }

    /* Indexed and EGA-text modes are not something this driver can draw into. */
    if (boot_info.framebuffer_type != 1) {
        kprintf("FB: unsupported framebuffer type %u\n", boot_info.framebuffer_type);
        return -1;
    }

    u32 width  = boot_info.framebuffer_width;
    u32 height = boot_info.framebuffer_height;
    u32 pitch  = boot_info.framebuffer_pitch;
    u8  bpp    = boot_info.framebuffer_bpp;

    if (!width || !height || (bpp != 16 && bpp != 24 && bpp != 32)) {
        kprintf("FB: refusing %ux%u at %u bpp\n", width, height, bpp);
        return -1;
    }
    if (pitch < width * (bpp / 8)) {
        kprintf("FB: pitch smaller than one scanline\n");
        return -1;
    }

    /* Map the whole surface. Guard against an overflow in the byte count. */
    u64 bytes = (u64)pitch * height;
    if (bytes == 0 || bytes > 256ULL * 1024 * 1024) {
        kprintf("FB: implausible framebuffer size\n");
        return -1;
    }

    u64 pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u64 i = 0; i < pages; i++) {
        u64 virt = FB_VIRT_BASE + i * PAGE_SIZE;
        u64 phys = (boot_info.framebuffer_addr + i * PAGE_SIZE) & ~(PAGE_SIZE - 1);
        if (!vmm_map_page(virt, phys, PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE)) {
            kprintf("FB: failed to map page %llu of %llu\n", i, pages);
            return -1;
        }
    }

    fb->phys_addr = boot_info.framebuffer_addr;
    fb->pitch     = pitch;
    fb->width     = width;
    fb->height    = height;
    fb->bpp       = bpp;
    fb->fb_type   = boot_info.framebuffer_type;
    fb->red_pos   = boot_info.framebuffer_red;
    fb->green_pos = boot_info.framebuffer_green;
    fb->blue_pos  = boot_info.framebuffer_blue;

    /* Only 32bpp can be handed out as a u32*; the other depths go through
     * fb_put_pixel(), which packs each pixel individually. */
    fb->pixels = (bpp == 32) ? (u32*)(uintptr_t)FB_VIRT_BASE : 0;

    /*
     * Derive masks from the reported bit positions.
     *
     * The positions cannot be taken on trust. The framebuffer tag this
     * bootloader emits declares blue at the same bit position as green, which
     * would collapse the two channels onto each other and leave blue at zero.
     * Treat any set of positions that collides, or that does not fit within
     * the pixel, as a bad tag and fall back to the layout 32bpp framebuffers
     * almost always use.
     */
    {
        u8 rb = (bpp == 16) ? 5 : 8;
        u8 gb = (bpp == 16) ? 6 : 8;
        u8 bb = (bpp == 16) ? 5 : 8;

        bool sane = (fb->red_pos < bpp) && (fb->green_pos < bpp) && (fb->blue_pos < bpp)
                 && (fb->red_pos + rb <= bpp)
                 && (fb->green_pos + gb <= bpp)
                 && (fb->blue_pos + bb <= bpp)
                 && (fb->red_pos   != fb->green_pos)
                 && (fb->green_pos != fb->blue_pos)
                 && (fb->red_pos   != fb->blue_pos);

        if (!sane) {
            kprintf("FB: implausible colour positions (r%u g%u b%u) at %ubpp, assuming XRGB\n",
                    fb->red_pos, fb->green_pos, fb->blue_pos, bpp);
            fb->red_pos = (bpp == 16) ? 11 : 16;
            fb->green_pos = (bpp == 16) ? 5 : 8;
            fb->blue_pos = 0;
        }
    }

    if (bpp == 32) {
        fb->red_mask   = mask_at(fb->red_pos,   8);
        fb->green_mask = mask_at(fb->green_pos, 8);
        fb->blue_mask  = mask_at(fb->blue_pos,  8);
    } else if (bpp == 16) {
        fb->red_mask   = mask_at(fb->red_pos,   5);
        fb->green_mask = mask_at(fb->green_pos, 6);
        fb->blue_mask  = mask_at(fb->blue_pos,  5);
    } else { /* 24bpp, packed 8/8/8 with no alpha byte */
        fb->red_mask   = mask_at(fb->red_pos,   8);
        fb->green_mask = mask_at(fb->green_pos, 8);
        fb->blue_mask  = mask_at(fb->blue_pos,  8);
    }

    fb->initialized = true;

    kprintf("FB: %ux%u %ubpp mapped at %p, %s%u%s%u%s\n",
               width, height, bpp, (void*)FB_VIRT_BASE,
               "R", fb->red_pos, " G", fb->green_pos, " B", fb->blue_pos);
    return 0;
}

void fb_put_pixel(u32 x, u32 y, u32 argb) {
    framebuffer_t* fb = &g_framebuffer;
    if (!fb->initialized) return;
    if (x >= fb->width || y >= fb->height) return;
    if (argb == FB_TRANSPARENT) return;

    u8* base = (u8*)(uintptr_t)FB_VIRT_BASE;
    u8* row  = base + (u64)y * fb->pitch;
    u32 px   = fb_pack(argb);

    if (fb->bpp == 32) {
        ((u32*)row)[x] = px;
    } else if (fb->bpp == 16) {
        ((u16*)row)[x] = (u16)px;
    } else { /* 24bpp is three bytes per pixel, not a 32-bit store */
        u8* p = row + (u64)x * 3;
        p[0] = (u8)(px & 0xFF);
        p[1] = (u8)((px >> 8) & 0xFF);
        p[2] = (u8)((px >> 16) & 0xFF);
    }
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 argb) {
    if (!g_framebuffer.initialized || argb == FB_TRANSPARENT) return;

    /* Clip to the screen. */
    if (x >= g_framebuffer.width || y >= g_framebuffer.height) return;
    if (x + w > g_framebuffer.width)  w = g_framebuffer.width - x;
    if (y + h > g_framebuffer.height) h = g_framebuffer.height - y;

    for (u32 py = y; py < y + h; py++) {
        for (u32 px = x; px < x + w; px++) {
            fb_put_pixel(px, py, argb);
        }
    }
}

void fb_draw_rect(u32 x, u32 y, u32 w, u32 h, u32 argb) {
    if (!g_framebuffer.initialized) return;
    if (w == 0 || h == 0) return;
    fb_fill_rect(x, y, w, 1, argb);
    fb_fill_rect(x, y + h - 1, w, 1, argb);
    fb_fill_rect(x, y, 1, h, argb);
    fb_fill_rect(x + w - 1, y, 1, h, argb);
}

void fb_clear(u32 argb) {
    if (!g_framebuffer.initialized) return;
    fb_fill_rect(0, 0, g_framebuffer.width, g_framebuffer.height, argb);
}

void fb_swap(void) {
    /* Single-buffered: the mapping is write-through, so there is no back
     * buffer to present. */
}
