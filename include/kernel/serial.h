/* serial.h - COM1 serial port debugging */
#pragma once
#include <kernel/types.h>

#define SERIAL_PORT_COM1 0x3F8

void serial_init(void);
void serial_putchar(char c);
void serial_write(const char* str);
void serial_writestring(const char* str);
void serial_printf(const char* fmt, ...);
void serial_puts(const char* str);
void serial_puthex(u32 num);

