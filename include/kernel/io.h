#ifndef KERNEL_IO_H
#define KERNEL_IO_H

#include <kernel/types.h>

void outb(u16 port, u8 value);
u8 inb(u16 port);
void outw(u16 port, u16 value);
u16 inw(u16 port);
void outl(u16 port, u32 value);
u32 inl(u16 port);
void io_wait(void);
void io_delay(void);

#endif // KERNEL_IO_H