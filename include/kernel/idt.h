#ifndef IDT_H
#define IDT_H

#include <kernel/types.h>

#define IDT_ENTRIES 256

/* Interrupt gate descriptor */
typedef struct {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed)) idt_entry_t;

/* IDT pointer for lidt */
typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) idt_ptr_t;

/* CPU register frame pushed by interrupt */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rbx, rdx, rcx, rax, rsi, rdi;
    u64 rip, cs, rflags, rsp, ss;
    u64 vector;
    u64 error_code;
    u64 cr2;
    /* ... additional fields if needed ... */
} reg_frame_t;

void idt_init(void);
void idt_set_gate(u8 vector, u64 handler, u8 ist, u8 type_attr);
void idt_dispatch_handler(reg_frame_t* frame);

#endif