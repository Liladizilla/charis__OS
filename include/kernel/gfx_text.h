/* gfx_text.h - Text rendering on the linear framebuffer */
#ifndef KERNEL_GFX_TEXT_H
#define KERNEL_GFX_TEXT_H

#include <kernel/types.h>
#include <kernel/fb.h>

void gfx_text_apply_preferences(void);
u32 gfx_text_get_scale(void);
u32 gfx_text_get_style(void);
void gfx_draw_char(u32 x, u32 y, char ch, u32 fg, u32 bg);
void gfx_draw_string(u32 x, u32 y, const char* s, u32 fg, u32 bg);
void gfx_draw_string_scaled(u32 x, u32 y, const char* s, u32 fg, u32 bg, u32 scale);
void gfx_draw_string_centered(u32 y, const char* s, u32 fg, u32 bg);
void gfx_text_size(const char* s, u32* out_w, u32* out_h);

#endif
