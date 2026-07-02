#include <kernel/serial.h>
#include <kernel/printf.h>
#include <kernel/types.h>
#include <stdarg.h>

#define SERIAL_DATA_PORT(base)          (base)
#define SERIAL_FIFO_COMMAND_PORT(base)  (base + 2)
#define SERIAL_LINE_COMMAND_PORT(base)  (base + 3)
#define SERIAL_MODEM_COMMAND_PORT(base) (base + 4)
#define SERIAL_LINE_STATUS_PORT(base)   (base + 5)

#define SERIAL_LINE_ENABLE_DLAB         0x80

void serial_puthex(u32 num) {
    char tmp[9];
    int i;
    for (i = 7; i >= 0; i--) {
        int nibble = (num >> (i * 4)) & 0xF;
        tmp[7 - i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    tmp[8] = '\0';
    serial_write(tmp);
}

void serial_init(void) {
    u16 port = SERIAL_PORT_COM1;

    // Disable interrupts
    outb(SERIAL_DATA_PORT(port) + 1, 0x00);

    // Enable DLAB
    outb(SERIAL_LINE_COMMAND_PORT(port), SERIAL_LINE_ENABLE_DLAB);

    // Set divisor to 12 (9600 baud)
    outb(SERIAL_DATA_PORT(port), 12);
    outb(SERIAL_DATA_PORT(port) + 1, 0x00);

    // 8 bits, no parity, one stop bit
    outb(SERIAL_LINE_COMMAND_PORT(port), 0x03);

    // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_FIFO_COMMAND_PORT(port), 0xC7);

    // IRQs enabled, RTS/DSR set
    outb(SERIAL_MODEM_COMMAND_PORT(port), 0x0B);
}

static int serial_transmit_empty(u16 port) {
    u8 status;
    status = inb(SERIAL_LINE_STATUS_PORT(port));
    return status & 0x20;
}

void serial_putchar(char c) {
    u16 port = SERIAL_PORT_COM1;
    while (!serial_transmit_empty(port));
    outb(SERIAL_DATA_PORT(port), (u8)c);
}

void serial_write(const char* str) {
    while (*str) {
        serial_putchar(*str++);
    }
}

void serial_writestring(const char* str) {
    serial_write(str);
}

void serial_puts(const char* str) {
    serial_write(str);
    serial_putchar('\n');
}

void serial_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args, serial_putchar);
    va_end(args);
}