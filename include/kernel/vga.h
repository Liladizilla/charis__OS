/* vga.h - VGA text mode driver */
#pragma once
#include <kernel/types.h>
#include <stdarg.h>

/* Standard VGA colors */
enum VGA_COLOR {
    VGA_BLACK = 0,
    VGA_BLUE = 1,
    VGA_GREEN = 2,
    VGA_CYAN = 3,
    VGA_RED = 4,
    VGA_MAGENTA = 5,
    VGA_BROWN = 6,
    VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8,
    VGA_LIGHT_BLUE = 9,
    VGA_LIGHT_GREEN = 10,
    VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12,
    VGA_LIGHT_MAGENTA = 13,
    VGA_LIGHT_BROWN = 14,
    VGA_WHITE = 15,
};

void vga_init(void);
void vga_putchar(char c);
void vga_write(const char* str);
void vga_writestring(const char* str);
void vga_clear(void);
void vga_setcolor(u8 fg, u8 bg);
void vga_printf(const char* fmt, ...);
void vga_scroll(void);
void vga_puts(const char* str);
void vga_enable_cursor(u8 cursor_start, u8 cursor_end);
void vga_update_hw_cursor(void);

