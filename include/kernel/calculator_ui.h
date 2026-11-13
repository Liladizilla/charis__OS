/* calculator_ui.h - Native window-managed integer calculator. */
#pragma once
#include <kernel/types.h>

void calculator_ui_open(void);
void calculator_ui_close(void);
bool calculator_ui_is_open(void);
void calculator_ui_render(void);
bool calculator_ui_handle_mouse(int x, int y);
bool calculator_ui_handle_key(u8 scancode, bool pressed);
void calculator_ui_set_bounds(u32 x, u32 y, u32 width, u32 height);
void calculator_ui_reset_bounds(void);
u32 calculator_ui_titlebar_height(void);
