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

static cwp1_test_image_t valid_cwp1 = {
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

typedef struct __attribute__((packed)) {
    u8 magic[4];
    u32 version;
    u32 image_count;
    u32 entry_size;
    u32 total_size;
} cwp2_test_header_t;

typedef struct __attribute__((packed)) {
    u32 wallpaper_id;
    u32 width;
    u32 height;
    u32 stride;
    u32 data_offset;
    u32 data_size;
} cwp2_test_entry_t;

typedef struct __attribute__((packed)) {
    cwp2_test_header_t header;
    cwp2_test_entry_t entries[2];
    u8 alpine_pixels[8];
    u8 forest_pixels[8];
} cwp2_test_pack_t;

static cwp2_test_pack_t valid_cwp2 = {
    .header = {
        .magic = { 'C', 'W', 'P', '2' },
        .version = 2,
        .image_count = 2,
        .entry_size = sizeof(cwp2_test_entry_t),
        .total_size = sizeof(cwp2_test_pack_t)
    },
    .entries = {
        { WALLPAPER_ALPINE, 2, 2, 4,
          sizeof(cwp2_test_header_t) + 2u * sizeof(cwp2_test_entry_t), 8 },
        { WALLPAPER_FOREST_SKY, 2, 2, 4,
          sizeof(cwp2_test_header_t) + 2u * sizeof(cwp2_test_entry_t) + 8u, 8 }
    },
    .alpine_pixels = {
        0x00, 0xF8, 0xE0, 0x07, 0x1F, 0x00, 0xFF, 0xFF
    },
    .forest_pixels = {
        0xE0, 0xFF, 0x00, 0xF8, 0x1F, 0xF8, 0x00, 0x00
    }
};

static void expect_background(const char* message) {
    for (u32 i = 0; i < 4; i++)
        expect(pixels[i] == CHARIS_UI_BACKGROUND, message);
}

static void test_cwp1_compatibility(void) {
    boot_info.wallpaper_addr = (u64)(uintptr_t)&valid_cwp1;
    boot_info.wallpaper_size = sizeof(valid_cwp1);
    wallpaper_draw(WALLPAPER_CHARIS);
    expect(pixels[0] == 0xFFFF0000u, "CWP1 red expands to opaque ARGB");
    expect(pixels[1] == 0xFF00FF00u, "CWP1 green expands to opaque ARGB");
    expect(pixels[2] == 0xFF0000FFu, "CWP1 blue expands to opaque ARGB");
    expect(pixels[3] == 0xFFFFFFFFu, "CWP1 white expands to opaque ARGB");

    valid_cwp1.magic[0] = 'X';
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("bad CWP1 signature must fall back to the default background");
    valid_cwp1.magic[0] = 'C';

    boot_info.wallpaper_size = sizeof(valid_cwp1) - 1u;
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("truncated CWP1 module must be rejected");
    boot_info.wallpaper_size = sizeof(valid_cwp1);

    valid_cwp1.version = 2;
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("unsupported CWP1 version must be rejected");
    valid_cwp1.version = 1;

    valid_cwp1.width = 0;
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("zero-width CWP1 image must be rejected");
    valid_cwp1.width = 2;

    valid_cwp1.width = 4097;
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("over-limit CWP1 dimensions must be rejected");
    valid_cwp1.width = 2;

    valid_cwp1.stride = 2;
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("CWP1 stride smaller than RGB565 row width must be rejected");
    valid_cwp1.stride = 4;

    valid_cwp1.data_size = 7;
    wallpaper_draw(WALLPAPER_CHARIS);
    expect_background("CWP1 payload size inconsistent with dimensions must be rejected");
    valid_cwp1.data_size = 8;

    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("legacy CWP1 modules must not alias newly added wallpaper IDs");
}

static void test_cwp2_selection(void) {
    boot_info.wallpaper_addr = (u64)(uintptr_t)&valid_cwp2;
    boot_info.wallpaper_size = sizeof(valid_cwp2);

    wallpaper_draw(WALLPAPER_ALPINE);
    expect(pixels[0] == 0xFFFF0000u, "CWP2 selects Alpine payload by stable wallpaper ID");
    expect(pixels[1] == 0xFF00FF00u, "CWP2 Alpine green pixel is decoded");
    expect(pixels[2] == 0xFF0000FFu, "CWP2 Alpine blue pixel is decoded");
    expect(pixels[3] == 0xFFFFFFFFu, "CWP2 Alpine white pixel is decoded");

    wallpaper_draw(WALLPAPER_FOREST_SKY);
    expect(pixels[0] == 0xFFFFFF00u, "CWP2 selects Forest Sky payload by wallpaper ID");
    expect(pixels[1] == 0xFFFF0000u, "CWP2 Forest Sky red pixel is decoded");
    expect(pixels[2] == 0xFFFF00FFu, "CWP2 Forest Sky magenta pixel is decoded");
    expect(pixels[3] == 0xFF000000u, "CWP2 Forest Sky black pixel is decoded");

    wallpaper_draw(WALLPAPER_CITYSCAPE);
    expect_background("missing CWP2 image ID must use the safe background");

    expect(wallpaper_count() == 8u, "legacy presets and three photo IDs are registered");
    expect(strcmp(wallpaper_name(WALLPAPER_ALPINE), "Alpine Village") == 0,
           "CWP2 wallpaper has a stable display name");
    expect(strcmp(wallpaper_name(WALLPAPER_CITYSCAPE), "Cityscape") == 0,
           "city wallpaper has a display name");
}

static void test_cwp2_rejection(void) {
    boot_info.wallpaper_addr = (u64)(uintptr_t)&valid_cwp2;
    boot_info.wallpaper_size = sizeof(valid_cwp2);

    u32 saved_size = valid_cwp2.header.total_size;
    valid_cwp2.header.total_size++;
    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("CWP2 total length mismatch is rejected");
    valid_cwp2.header.total_size = saved_size;

    u32 saved_version = valid_cwp2.header.version;
    valid_cwp2.header.version = 3;
    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("unsupported CWP2 version is rejected");
    valid_cwp2.header.version = saved_version;

    u32 saved_offset = valid_cwp2.entries[0].data_offset;
    valid_cwp2.entries[0].data_offset = sizeof(cwp2_test_header_t);
    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("CWP2 payload cannot overlap the directory");
    valid_cwp2.entries[0].data_offset = saved_offset;

    saved_offset = valid_cwp2.entries[1].data_offset;
    valid_cwp2.entries[1].data_offset = valid_cwp2.entries[0].data_offset;
    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("CWP2 payload entries cannot overlap");
    valid_cwp2.entries[1].data_offset = saved_offset;

    u32 saved_id = valid_cwp2.entries[1].wallpaper_id;
    valid_cwp2.entries[1].wallpaper_id = valid_cwp2.entries[0].wallpaper_id;
    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("CWP2 wallpaper IDs must be unique");
    valid_cwp2.entries[1].wallpaper_id = saved_id;

    u32 saved_payload = valid_cwp2.entries[1].data_size;
    valid_cwp2.entries[1].data_size--;
    wallpaper_draw(WALLPAPER_FOREST_SKY);
    expect_background("CWP2 payload sizes must match image dimensions");
    valid_cwp2.entries[1].data_size = saved_payload;

    wallpaper_draw(WALLPAPER_ALPINE);
    boot_info.wallpaper_size = sizeof(valid_cwp2) - 1u;
    wallpaper_draw(WALLPAPER_ALPINE);
    expect_background("truncated CWP2 payload is rejected before pixel access");
    boot_info.wallpaper_size = sizeof(valid_cwp2);
}

int main(void) {
    expect(wallpaper_count() == 8u, "wallpaper catalog exposes all eight options");
    test_cwp1_compatibility();
    test_cwp2_selection();
    test_cwp2_rejection();
    puts("CWP1/CWP2 wallpaper module tests: PASS");
    return 0;
}
