/* terminal_ui.h - Native framebuffer terminal */
#pragma once

#include <kernel/types.h>

void terminal_open(void);
void terminal_close(void);
bool terminal_is_open(void);
void terminal_handle_char(char ch);
bool terminal_handle_mouse(int x, int y);
void terminal_render(void);
