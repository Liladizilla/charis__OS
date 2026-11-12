/* browser.h - CharisOS built-in browser shell */
#pragma once
#include <kernel/types.h>

void browser_open(void);
void browser_close(void);
bool browser_is_open(void);
void browser_render(void);
bool browser_handle_mouse(int x, int y);
bool browser_handle_key(u8 scancode, bool pressed);
void browser_set_bounds(u32 x,u32 y,u32 width,u32 height);
void browser_reset_bounds(void);
u32 browser_titlebar_height(void);
