/* irq.h - IRQ and PIC management */
#pragma once
#include <kernel/types.h>
#include <kernel/idt.h>

#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

#define IRQ_OFFSET      32

#define IRQ_TIMER       0
#define IRQ_KEYBOARD    1
#define IRQ_CASCADE     2
#define IRQ_COM2        3
#define IRQ_COM1        4
#define IRQ_LPT2        5
#define IRQ_FLOPPY      6
#define IRQ_LPT1        7
#define IRQ_RTC         8
#define IRQ_MOUSE       12
#define IRQ_ATA1        14
#define IRQ_ATA2        15

void pic_init(void);
void pic_send_eoi(u8 irq);
void pic_mask_irq(u8 irq);
void pic_unmask_irq(u8 irq);

void irq_init(void);
void irq_register_handler(u8 irq, void (*handler)(reg_frame_t*));
void irq_dispatch(u8 irq, reg_frame_t* frame);

