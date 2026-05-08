/* printf.h - CharisOS kernel formatted output */
#pragma once

#include <kernel/types.h>

void kvprintf(const char* fmt, va_list args, void (*putch)(char));
void kprintf(const char* fmt, ...);
