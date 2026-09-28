/* gfx_text.h - Text rendering on the linear framebuffer */
#ifndef KERNEL_GFX_TEXT_H
#define KERNEL_GFX_TEXT_H

#include <kernel/types.h>
#include <kernel/fb.h>

void gfx_draw_char(u32 x, u32 y, char ch, u32 fg, u32 bg);
void gfx_draw_string(u32 x, u32 y, const char* s, u32 fg, u32 bg);
void gfx_draw_string_centered(u32 y, const char* s, u32 fg, u32 bg);
void gfx_text_size(const char* s, u32* out_w, u32* out_h);

#endif
