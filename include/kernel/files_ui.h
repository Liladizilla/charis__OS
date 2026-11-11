/* files_ui.h - Native read-only FAT32 file browser. */
#pragma once
#include <kernel/types.h>

void files_open(void);
void files_close(void);
bool files_is_open(void);
bool files_handle_key(u8 scancode, bool pressed);
bool files_handle_mouse(int x, int y);
void files_render(void);
void files_set_bounds(u32 x,u32 y,u32 width,u32 height);
void files_reset_bounds(void);
u32 files_titlebar_height(void);
