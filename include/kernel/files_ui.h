/* files_ui.h - Native read-only FAT32 file browser. */
#pragma once
#include <kernel/types.h>

void files_open(void);
void files_close(void);
bool files_is_open(void);
bool files_handle_key(u8 scancode, bool pressed);
bool files_handle_mouse(int x, int y);
void files_render(void);
