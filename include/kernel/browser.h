/* browser.h - CharisOS built-in browser shell */
#pragma once
#include <kernel/types.h>

void browser_open(void);
void browser_close(void);
bool browser_is_open(void);
void browser_render(void);
bool browser_handle_mouse(int x, int y);
bool browser_handle_key(u8 scancode, bool pressed);
