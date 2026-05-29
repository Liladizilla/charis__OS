#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/mouse.h>
#include <kernel/input.h>

static void (*irq_handlers[16])(reg_frame_t*) = {0};

// PIC Initialization
void pic_init(void) {
    // ICW1: Edge-triggered, cascade mode, ICW4 needed
    outb(PIC1_COMMAND, 0x11);
    io_delay();
    outb(PIC2_COMMAND, 0x11);
    io_delay();
    
    // ICW2: Interrupt vector offsets
    outb(PIC1_DATA, IRQ_OFFSET);
    io_delay();
    outb(PIC2_DATA, IRQ_OFFSET + 8);
    io_delay();
    
    // ICW3: Cascade identity
    outb(PIC1_DATA, 0x04); // PIC1 has IRQ2 connected to PIC2
    io_delay();
    outb(PIC2_DATA, 0x02); // PIC2 cascade identity
    io_delay();
    
    // ICW4: 80x86 mode
    outb(PIC1_DATA, 0x01);
    io_delay();
    outb(PIC2_DATA, 0x01);
    io_delay();
    
    // Mask all IRQs initially
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_mask_irq(u8 irq) {
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 value = inb(port);
    value |= (irq < 8) ? (1 << irq) : (1 << (irq - 8));
    outb(port, value);
}

void pic_unmask_irq(u8 irq) {
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 value = inb(port);
    value &= (irq < 8) ? ~(1 << irq) : ~(1 << (irq - 8));
    outb(port, value);
}

// Mouse IRQ handler (IRQ 12)
static void mouse_irq_handler(reg_frame_t* frame) {
    (void)frame;
    mouse_handler();
    pic_send_eoi(12);
}

void irq_init(void) {
    pic_init();
    irq_register_handler(12, mouse_irq_handler); // Mouse IRQ
    vga_puts("PIC initialized\n");
}

void irq_register_handler(u8 irq, void (*handler)(reg_frame_t*)) {
    irq_handlers[irq] = handler;
    pic_unmask_irq(irq);
}

// Called from idt_dispatch_handler
void irq_dispatch(u8 irq, reg_frame_t* frame) {
    if (irq_handlers[irq]) {
        irq_handlers[irq](frame);
    }
}