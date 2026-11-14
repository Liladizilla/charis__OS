/* logo.c - CharisOS startup identity.
 *
 * Text-only systems retain the CP437 banner as a fallback. Graphical systems
 * render the official transparent C-and-sword emblem from a small embedded
 * RLE asset; startup never depends on a filesystem image decoder.
 */

#include <kernel/logo.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/charis_logo_bitmap.h>

#define H  "\xCD"   /* ═ */
#define V  "\xBA"   /* ║ */
#define B  "\xDB"   /* █ */

static const char* const LOGO_LINES[] = {
    "\xC9" H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
        H H H H H H H H H H H H H H H H H H H H H H H H H H "\xBB",

    V "    " B B B B " " B "   " B "  " B B B "  " B B B B B "  " B B B B
        "  " B B B "   " B B B B "   " V,

    V "   " B "     " B "   " B " " B "   " B " " B "   " B "   "
        B "   " B "     " B "   " B " " B "       " V,

    V "   " B "     " B "   " B " " B "   " B " " B "   " B "   "
        B "   " B "     " B "   " B " " B "       " V,

    V "   " B "     " B B B B B " " B B B B B " " B B B B B "    " B
        "    " B B B "  " B "   " B "  " B B B "    " V,

    V "   " B "     " B "   " B " " B " " B "     " B "   " B "   "
        B "     " B "   " B "   " B "     " B "   " V,

    V "   " B "     " B "   " B " " B "  " B "    " B "       " B " "
        "   " B "   " B "     " B "   " V,

    V "    " B B B B " " B "   " B " " B "   " B " " B B B B B " "
        B B B B "   " B B B "  " B B B B "    " V,

    V "                                                      " V,

    V "  x86_64 kernel  //  built from scratch in C & NASM   " V,

    V "               v1.0  -  Nairobi, Kenya                " V,

    "\xC8" H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
        H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H H
        H H H H H H H H H H H H H "\xBC",
};

#define LOGO_LINES_N ((u32)(sizeof(LOGO_LINES) / sizeof(LOGO_LINES[0])))

/* Text console. */
void logo_print(void) {
    for (u32 i = 0; i < LOGO_LINES_N; i++) {
        vga_puts(LOGO_LINES[i]);
        vga_puts("\n");
    }
}

/* Compact boot-safe palette sampled from the official emblem. */
#define EMBLEM_SILVER FB_ARGB(0xFF, 0xD9, 0xD1, 0xC4)
#define EMBLEM_GOLD   FB_ARGB(0xFF, 0xB8, 0x87, 0x43)
#define EMBLEM_SHADE  FB_ARGB(0xFF, 0x3A, 0x2C, 0x1A)

static const char* boot_stage = "INITIALIZING KERNEL";

void logo_set_stage(const char* stage) {
    if (!stage || !*stage) return;

    boot_stage = stage;
    serial_puts("[BOOT_STAGE] ");
    serial_puts(stage);
    serial_puts("\n");

    if (g_framebuffer.initialized) logo_draw_gfx_centered();
}

static u32 emblem_scale(void) {
    if (g_framebuffer.width >= 1600u && g_framebuffer.height >= 900u) return 3u;
    if (g_framebuffer.width >= 820u && g_framebuffer.height >= 620u) return 2u;
    return 1u;
}

static u32 splash_panel_height(u32 scale) {
    return 24u + CHARIS_LOGO_HEIGHT * scale + 16u + 24u + 10u + 24u + 18u;
}

static void draw_emblem(u32 x, u32 y, u32 scale) {
    const u32 total_pixels = CHARIS_LOGO_WIDTH * CHARIS_LOGO_HEIGHT;
    u32 pixel = 0;

    for (u32 i = 0; i < CHARIS_LOGO_RLE_SIZE; i++) {
        u8 token = charis_logo_rle[i];
        u32 run = token >> 2;
        u32 palette = token & 3u;

        if (!run || pixel >= total_pixels || run > total_pixels - pixel)
            return;

        while (run) {
            u32 row = pixel / CHARIS_LOGO_WIDTH;
            u32 col = pixel - row * CHARIS_LOGO_WIDTH;
            u32 span = CHARIS_LOGO_WIDTH - col;
            if (span > run) span = run;

            u32 colour = FB_TRANSPARENT;
            if (palette == 1u) colour = EMBLEM_SILVER;
            else if (palette == 2u) colour = EMBLEM_GOLD;
            else if (palette == 3u) colour = EMBLEM_SHADE;

            if (colour != FB_TRANSPARENT) {
                fb_fill_rect(x + col * scale, y + row * scale,
                             span * scale, scale, colour);
            }
            pixel += span;
            run -= span;
        }
    }
}

void logo_draw_gfx(u32 y) {
    if (!g_framebuffer.initialized) return;

    u32 scale = emblem_scale();
    u32 image_w = CHARIS_LOGO_WIDTH * scale;
    u32 image_h = CHARIS_LOGO_HEIGHT * scale;
    u32 panel_w = image_w + 72u;
    u32 panel_h = splash_panel_height(scale);

    if (panel_w > g_framebuffer.width || panel_h > g_framebuffer.height) {
        scale = 1u;
        image_w = CHARIS_LOGO_WIDTH;
        image_h = CHARIS_LOGO_HEIGHT;
        panel_w = image_w + 72u;
        panel_h = splash_panel_height(scale);
    }

    u32 panel_x = (g_framebuffer.width > panel_w)
        ? (g_framebuffer.width - panel_w) / 2u : 0u;
    if (panel_w > g_framebuffer.width) panel_w = g_framebuffer.width;
    if (y > g_framebuffer.height) y = g_framebuffer.height;
    if (panel_h > g_framebuffer.height - y)
        y = g_framebuffer.height > panel_h
            ? (g_framebuffer.height - panel_h) / 2u : 0u;

    fb_fill_rect(panel_x, y, panel_w, panel_h,
                 FB_ARGB(0xFF, 0x0D, 0x11, 0x17));
    fb_draw_rect(panel_x, y, panel_w, panel_h,
                 FB_ARGB(0xFF, 0x65, 0x53, 0x37));

    u32 image_x = panel_x + (panel_w - image_w) / 2u;
    u32 image_y = y + 24u;
    draw_emblem(image_x, image_y, scale);

    u32 title_y = image_y + image_h + 16u;
    gfx_draw_string_centered(title_y, "CHARISOS",
        FB_ARGB(0xFF, 0xE7, 0xC0, 0x80), FB_TRANSPARENT);
    gfx_draw_string_centered(title_y + 28u, "SECURITY FIRST",
        FB_ARGB(0xFF, 0xA5, 0xAD, 0xBA), FB_TRANSPARENT);
    gfx_draw_string_centered(title_y + 56u, boot_stage,
        FB_ARGB(0xFF, 0x9C, 0xA5, 0xB3), FB_TRANSPARENT);
}

void logo_draw_gfx_centered(void) {
    if (!g_framebuffer.initialized) return;
    u32 panel_h = splash_panel_height(emblem_scale());
    u32 y = g_framebuffer.height > panel_h
        ? (g_framebuffer.height - panel_h) / 2u : 0u;
    logo_draw_gfx(y);
}
