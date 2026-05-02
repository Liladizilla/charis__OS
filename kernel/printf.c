#include <kernel/printf.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/types.h>
#include <stdarg.h>

static void output_string(const char* str, void (*putch)(char)) {
    if (str == NULL) {
        str = "(null)";
    }
    while (*str) {
        putch(*str++);
    }
}

static void output_padding(int width, bool zero, bool left, void (*putch)(char), int len) {
    int pad = width - len;
    if (pad <= 0) return;
    if (!left) {
        char fill = zero ? '0' : ' ';
        while (pad-- > 0) {
            putch(fill);
        }
    }
}

static void output_padding_after(int width, bool left, void (*putch)(char), int len) {
    int pad = width - len;
    if (pad <= 0 || !left) return;
    while (pad-- > 0) {
        putch(' ');
    }
}

static char* format_unsigned(u64 value, char* buf, int base, bool uppercase) {
    if (buf == NULL) return NULL;
    if (base < 2 || base > 36) {
        buf[0] = '\0';
        return buf;
    }

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    int pos = 0;
    char tmp[65];
    while (value != 0) {
        int digit = value % base;
        if (digit < 10) {
            tmp[pos++] = '0' + digit;
        } else {
            tmp[pos++] = (uppercase ? 'A' : 'a') + (digit - 10);
        }
        value /= base;
    }

    int out = 0;
    while (pos > 0) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
    return buf;
}

static char* format_signed(s64 value, char* buf) {
    if (buf == NULL) return NULL;
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    bool negative = false;
    u64 abs_val;
    if (value < 0) {
        negative = true;
        abs_val = (u64)(-(value + 1)) + 1;
    } else {
        abs_val = (u64)value;
    }

    char tmp[65];
    int pos = 0;
    while (abs_val != 0) {
        tmp[pos++] = '0' + (abs_val % 10);
        abs_val /= 10;
    }

    int out = 0;
    if (negative) {
        buf[out++] = '-';
    }
    while (pos > 0) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
    return buf;
}

void kvprintf(const char* fmt, va_list args, void (*putch)(char)) {
    if (fmt == NULL || putch == NULL) return;

    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            putch(*p++);
            continue;
        }

        p++;
        if (*p == '%') {
            putch('%');
            p++;
            continue;
        }

        bool left_align = false;
        bool zero_pad = false;
        int width = 0;

        if (*p == '-') {
            left_align = true;
            p++;
        }

        if (*p == '0') {
            zero_pad = true;
            p++;
        }

        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        bool long_long = false;
        if (*p == 'l' && *(p + 1) == 'l') {
            long_long = true;
            p += 2;
        }

        char spec = *p++;
        char numbuf[66];

        switch (spec) {
            case 's': {
                const char* str = va_arg(args, const char*);
                int len = (int)kstrlen(str ? str : "(null)");
                if (width > len && !left_align) {
                    output_padding(width, zero_pad, left_align, putch, len);
                }
                output_string(str, putch);
                if (width > len && left_align) {
                    output_padding_after(width, left_align, putch, len);
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                if (width > 1 && !left_align) {
                    output_padding(width, zero_pad, left_align, putch, 1);
                }
                putch(c);
                if (width > 1 && left_align) {
                    output_padding_after(width, left_align, putch, 1);
                }
                break;
            }
            case 'd': {
                if (long_long) {
                    s64 value = va_arg(args, long long);
                    bool negative = value < 0;
                    format_signed(value, numbuf);
                    int len = (int)kstrlen(numbuf);
                    if (width > len) {
                        if (zero_pad && negative && !left_align) {
                            putch('-');
                            output_padding(width - 1, true, left_align, putch, len - 1);
                            output_string(numbuf + 1, putch);
                        } else {
                            output_padding(width, zero_pad && !negative, left_align, putch, len);
                            output_string(numbuf, putch);
                        }
                    } else {
                        output_string(numbuf, putch);
                    }
                    if (width > len && left_align) {
                        output_padding_after(width, left_align, putch, len);
                    }
                } else {
                    int value = va_arg(args, int);
                    s64 signed_value = value;
                    bool negative = signed_value < 0;
                    format_signed(signed_value, numbuf);
                    int len = (int)kstrlen(numbuf);
                    if (width > len) {
                        if (zero_pad && negative && !left_align) {
                            putch('-');
                            output_padding(width - 1, true, left_align, putch, len - 1);
                            output_string(numbuf + 1, putch);
                        } else {
                            output_padding(width, zero_pad && !negative, left_align, putch, len);
                            output_string(numbuf, putch);
                        }
                    } else {
                        output_string(numbuf, putch);
                    }
                    if (width > len && left_align) {
                        output_padding_after(width, left_align, putch, len);
                    }
                }
                break;
            }
            case 'u': {
                u64 value = long_long ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                format_unsigned(value, numbuf, 10, false);
                int len = (int)kstrlen(numbuf);
                if (width > len && !left_align) {
                    output_padding(width, zero_pad, left_align, putch, len);
                }
                output_string(numbuf, putch);
                if (width > len && left_align) {
                    output_padding_after(width, left_align, putch, len);
                }
                break;
            }
            case 'x':
            case 'X': {
                bool uppercase = (spec == 'X');
                u64 value = long_long ? va_arg(args, unsigned long long) : va_arg(args, unsigned int);
                format_unsigned(value, numbuf, 16, uppercase);
                int len = (int)kstrlen(numbuf);
                if (width > len && !left_align) {
                    output_padding(width, zero_pad, left_align, putch, len);
                }
                output_string(numbuf, putch);
                if (width > len && left_align) {
                    output_padding_after(width, left_align, putch, len);
                }
                break;
            }
            case 'p': {
                void* ptr = va_arg(args, void*);
                u64 value = (u64)ptr;
                format_unsigned(value, numbuf, 16, false);
                int len = (int)kstrlen(numbuf) + 2;
                if (width > len && !left_align) {
                    output_padding(width, zero_pad, left_align, putch, len);
                }
                output_string("0x", putch);
                output_string(numbuf, putch);
                if (width > len && left_align) {
                    output_padding_after(width, left_align, putch, len);
                }
                break;
            }
            default:
                putch(spec);
                break;
        }
    }
}

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    kvprintf(fmt, args, vga_putchar);
    kvprintf(fmt, args_copy, serial_putchar);
    va_end(args_copy);
    va_end(args);
}
