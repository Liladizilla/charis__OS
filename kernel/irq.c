#include <kernel/irq.h>
#include <kernel/io.h>
#include <kernel/vga.h>
#include <kernel/mouse.h>
#include <kernel/input.h>
#include <kernel/diagnostics.h>

static void (*irq_handlers[16])(reg_frame_t*) = {0};
static volatile u32 g_mouse_irq_count = 0;
static volatile u32 g_irq_spurious7 = 0;
static volatile u32 g_irq_spurious15 = 0;
static volatile u32 g_irq_unhandled = 0;

u32 mouse_irq_count(void) { return g_mouse_irq_count; }
u32 irq_spurious7_count(void) { return g_irq_spurious7; }
u32 irq_spurious15_count(void) { return g_irq_spurious15; }
u32 irq_unhandled_count(void) { return g_irq_unhandled; }

/* Save the caller's interrupt state. PIC mask/EOI register pairs must not be
 * interleaved with another local interrupt handler. This kernel is single-CPU. */
static inline u64 pic_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void pic_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

static void pic_write_mask(u8 irq, bool masked) {
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 bit = (u8)(1U << ((irq < 8) ? irq : (irq - 8)));
    u8 value = inb(port);
    value = masked ? (u8)(value | bit) : (u8)(value & (u8)~bit);
    outb(port, value);

    /* A slave interrupt also needs the master's cascade input enabled. */
    if (!masked && irq >= 8) {
        value = inb(PIC1_DATA);
        outb(PIC1_DATA, (u8)(value & (u8)~(1U << IRQ_CASCADE)));
    }
}

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

    // Mask all IRQs initially.
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    // Keep the master cascade open so slave IRQs can reach the CPU.
    pic_unmask_irq(IRQ_CASCADE);
}

void pic_send_eoi(u8 irq) {
    if (irq >= 16) return;

    u64 flags = pic_irq_save();
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
    pic_irq_restore(flags);
}

void pic_mask_irq(u8 irq) {
    if (irq >= 16) return;
    u64 flags = pic_irq_save();
    pic_write_mask(irq, true);
    pic_irq_restore(flags);
}

void pic_unmask_irq(u8 irq) {
    if (irq >= 16) return;
    u64 flags = pic_irq_save();
    pic_write_mask(irq, false);
    pic_irq_restore(flags);
}

// Mouse IRQ handler (IRQ 12)
static void mouse_irq_handler(reg_frame_t* frame) {
    (void)frame;
    g_mouse_irq_count++;
    mouse_handler();
    pic_send_eoi(IRQ_MOUSE);
}

void irq_init(void) {
    pic_init();
    irq_register_handler(IRQ_MOUSE, mouse_irq_handler);
    vga_puts("PIC initialized\n");
}

void irq_register_handler(u8 irq, void (*handler)(reg_frame_t*)) {
    /* The legacy PIC exposes exactly 16 IRQ lines. Reject invalid indices
     * before touching the fixed handler table or PIC mask registers. */
    if (irq >= 16 || !handler) return;

    u64 flags = pic_irq_save();
    irq_handlers[irq] = handler;
    pic_write_mask(irq, false);
    pic_irq_restore(flags);
}

/* IRQ7 and IRQ15 can be raised spuriously by the 8259 PIC. Read the in-service
 * register, not the request register, before dispatching either vector. */
static bool pic_irq_is_spurious(u8 irq) {
    if (irq == 7) {
        outb(PIC1_COMMAND, 0x0B); /* OCW3: read master ISR */
        return (inb(PIC1_COMMAND) & 0x80) == 0;
    }
    if (irq == 15) {
        outb(PIC2_COMMAND, 0x0B); /* OCW3: read slave ISR */
        return (inb(PIC2_COMMAND) & 0x80) == 0;
    }
    return false;
}

// Called from idt_dispatch_handler. Registered handlers own their EOI.
void irq_dispatch(u8 irq, reg_frame_t* frame) {
    if (irq >= 16) return;

    if (irq == 7 && pic_irq_is_spurious(irq)) {
        g_irq_spurious7++;
        return; /* A spurious master IRQ7 has no in-service bit to clear. */
    }

    if (irq == 15 && pic_irq_is_spurious(irq)) {
        g_irq_spurious15++;
        /* A spurious slave IRQ15 still placed the master cascade in service. */
        outb(PIC1_COMMAND, PIC_EOI);
        return;
    }

    diag_note_interrupt();
    void (*handler)(reg_frame_t*) = irq_handlers[irq];
    if (handler) {
        handler(frame);
    } else {
        g_irq_unhandled++;
        /* An unhandled genuine line still needs acknowledging or the PIC can
         * block later interrupts at the same or lower priority. */
        pic_send_eoi(irq);
    }
}
