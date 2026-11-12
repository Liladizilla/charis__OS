/* terminal_ui.h - Native framebuffer terminal */
#pragma once

#include <kernel/types.h>

void terminal_open(void);
void terminal_close(void);
bool terminal_is_open(void);
void terminal_handle_char(char ch);
bool terminal_handle_mouse(int x, int y);
void terminal_render(void);
void terminal_set_bounds(u32 x, u32 y, u32 width, u32 height);
void terminal_reset_bounds(void);
u32 terminal_titlebar_height(void);
