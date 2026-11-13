#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/wallpaper.h>
#include <kernel/bootmem.h>
#include <kernel/fb.h>
#include <kernel/ui_theme.h>

static u32 pixels[4];

framebuffer_t g_framebuffer = {
    .width = 2,
    .height = 2,
    .bpp = 32,
    .initialized = true
};
bootmem_display_t boot_info;

void fb_clear(u32 colour) {
    for (u32 i = 0; i < 4; i++) pixels[i] = colour;
}

void fb_put_pixel(u32 x, u32 y, u32 colour) {
    if (x < 2 && y < 2) pixels[y * 2 + x] = colour;
}

void fb_fill_rect(u32 x, u32 y, u32 width, u32 height, u32 colour) {
    for (u32 row = 0; row < height; row++)
        for (u32 col = 0; col < width; col++)
            fb_put_pixel(x + col, y + row, colour);
}

void* kmemcpy(void* destination, const void* source, usize bytes) {
    return memcpy(destination, source, bytes);
}

static void expect(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u32 version;
    u32 width;
    u32 height;
    u32 stride;
    u32 data_size;
    u8 pixels[8];
} cwp1_test_image_t;

static cwp1_test_image_t valid_image = {
    .magic = { 'C', 'W', 'P', '1' },
    .version = 1,
    .width = 2,
    .height = 2,
    .stride = 4,
    .data_size = 8,
    .pixels = {
        0x00, 0xF8, /* red */
        0xE0, 0x07, /* green */
        0x1F, 0x00, /* blue */
        0xFF, 0xFF  /* white */
    }
};

int main(void) {
    boot_info.wallpaper_addr = (u64)(uintptr_t)&valid_image;
    boot_info.wallpaper_size = sizeof(valid_image);
    wallpaper_draw(WALLPAPER_CHARIS);
    expect(pixels[0] == 0xFFFF0000u, "RGB565 red expands to opaque ARGB");
    expect(pixels[1] == 0xFF00FF00u, "RGB565 green expands to opaque ARGB");
    expect(pixels[2] == 0xFF0000FFu, "RGB565 blue expands to opaque ARGB");
    expect(pixels[3] == 0xFFFFFFFFu, "RGB565 white expands to opaque ARGB");

    valid_image.magic[0] = 'X';
    for (u32 i = 0; i < 4; i++) pixels[i] = 0;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "invalid wallpaper module fails closed to the default background");

    valid_image.magic[0] = 'C';
    boot_info.wallpaper_size = sizeof(valid_image) - 1u;
    for (u32 i = 0; i < 4; i++) pixels[i] = 0;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "truncated wallpaper module is rejected before pixel access");

    boot_info.wallpaper_size = sizeof(valid_image);
    valid_image.version = 2;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "unsupported CWP1 versions are rejected");

    valid_image.version = 1;
    valid_image.width = 0;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "zero-width images are rejected");

    valid_image.width = 4097;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "dimensions above the kernel limit are rejected");

    valid_image.width = 2;
    valid_image.stride = 2;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "row strides smaller than RGB565 row width are rejected");

    valid_image.stride = 4;
    valid_image.data_size = 7;
    wallpaper_draw(WALLPAPER_CHARIS);
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND,
               "payload sizes that disagree with dimensions are rejected");

    puts("CWP1 wallpaper module tests: PASS");
    return 0;
}
