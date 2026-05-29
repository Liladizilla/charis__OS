/* psf.h - PC Screen Font renderer */
#pragma once
#include <kernel/types.h>

#define PSF_MAGIC 0x864AB572

typedef struct {
    u32 magic;
    u32 version;
    u32 headersize;
    u32 flags;
    u32 numglyph;
    u32 bytesperglyph;
    u32 height;
    u32 width;
    // data follows...
} __attribute__((packed)) psf2_header_t;

extern psf2_header_t* g_psf_font;
extern bool g_font_loaded;

// Load PSF font from memory (Multiboot module)
int psf_load(u8* data);

// Draw character at position
void psf_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg);

// Draw string
void psf_draw_string(u32 x, u32 y, const char* str, u32 fg, u32 bg);

// Get font dimensions
u32 psf_char_width(void);
u32 psf_char_height(void);