#include <kernel/vga.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <kernel/string.h>
#include <stdarg.h>
#include <kernel/io.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_SIZE (VGA_WIDTH * VGA_HEIGHT)

static u16* vga_buffer = (u16*)0xB8000;
static u8 vga_color = 0x07; // Light grey on black (easier on low-end displays)
static u8 vga_bg_color = VGA_BLACK;
static u8 vga_fg_color = VGA_LIGHT_GREY;
static u32 vga_cursor = 0;

/* Optimized color palette for better visibility on low-end hardware */
#define COLOR_INFO    VGA_LIGHT_GREY
#define COLOR_ERROR   VGA_LIGHT_RED
#define COLOR_SUCCESS VGA_LIGHT_GREEN
#define COLOR_WARNING VGA_LIGHT_BROWN

void vga_init(void) {
    vga_clear();
    vga_enable_cursor(14, 15);
}

void vga_clear(void) {
    for (u32 i = 0; i < VGA_SIZE; i++) {
        vga_buffer[i] = (u16)' ' | ((u16)vga_color << 8);
    }
    vga_cursor = 0;
    vga_update_hw_cursor();
}

void vga_setcolor(u8 fg, u8 bg) {
    vga_fg_color = fg & 0x0F;
    vga_bg_color = bg & 0x07;
    vga_color = vga_fg_color | (vga_bg_color << 4);
}

void vga_setcolor_pair(u8 color) {
    vga_color = color & 0x7F;
    vga_fg_color = vga_color & 0x0F;
    vga_bg_color = (vga_color >> 4) & 0x07;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_cursor = (vga_cursor / VGA_WIDTH + 1) * VGA_WIDTH;
    } else if (c == '\r') {
        vga_cursor = (vga_cursor / VGA_WIDTH) * VGA_WIDTH;
    } else if (c == '\b') {
        if (vga_cursor > 0) {
            vga_cursor--;
            vga_buffer[vga_cursor] = (u16)' ' | ((u16)vga_color << 8);
        }
    } else {
        vga_buffer[vga_cursor] = (u16)c | ((u16)vga_color << 8);
        vga_cursor++;
    }

    if (vga_cursor >= VGA_SIZE) {
        vga_scroll();
        vga_cursor = (VGA_HEIGHT - 1) * VGA_WIDTH;
    }
    vga_update_hw_cursor();
}

static void vga_update_hw_cursor(void) {
    u16 pos = vga_cursor;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void vga_enable_cursor(u8 cursor_start, u8 cursor_end) {
    // Enable cursor using correct port access
    outb(0x3D4, 0x0A);
    outb(0x3D5, (u8)((inb(0x3D5) & 0xC0) | cursor_start));
    
    outb(0x3D4, 0x0B);
    outb(0x3D5, (u8)((inb(0x3D5) & 0xE0) | cursor_end));
}

void vga_write(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_writestring(const char* str) {
    vga_write(str);
}

void vga_puts(const char* str) {
    vga_write(str);
    vga_putchar('\n');
}

void vga_scroll(void) {
    for (u32 i = 0; i < VGA_SIZE - VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }
    for (u32 i = VGA_SIZE - VGA_WIDTH; i < VGA_SIZE; i++) {
        vga_buffer[i] = (u16)' ' | ((u16)vga_color << 8);
    }
    vga_update_hw_cursor();
}

void vga_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args, vga_putchar);
    va_end(args);
}

/* Colored output functions for better UI on low-end displays */
void vga_puts_info(const char* str) {
    u8 old_color = vga_color;
    vga_setcolor(COLOR_INFO, vga_bg_color);
    vga_puts(str);
    vga_setcolor_pair(old_color);
}

void vga_puts_error(const char* str) {
    u8 old_color = vga_color;
    vga_setcolor(COLOR_ERROR, vga_bg_color);
    vga_puts(str);
    vga_setcolor_pair(old_color);
}

void vga_puts_success(const char* str) {
    u8 old_color = vga_color;
    vga_setcolor(COLOR_SUCCESS, vga_bg_color);
    vga_puts(str);
    vga_setcolor_pair(old_color);
}
