#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

#define IDT_ENTRIES 256
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

extern u64 isr_table[256];

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base = (u64)&idt;

    // Clear IDT
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Load ISR stubs from assembly
    for (int i = 0; i < 256; i++) {
        if (isr_table[i]) {
            idt_set_gate(i, isr_table[i], 0, 0x8E); // Interrupt gate
        }
    }

    // Load IDT
    asm volatile("lidt %0" : : "m"(idt_ptr));

    vga_puts("IDT initialized\n");
}

void idt_set_gate(u8 vector, u64 handler, u8 ist, u8 type_attr) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08; // Kernel code segment
    idt[vector].ist = ist;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].zero = 0;
}

void idt_dispatch_handler(reg_frame_t* frame) {
    if (frame->vector == 0x80) {
        // System call
        frame->rax = syscall_dispatch(frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);
        return;
    }

    if (frame->vector < 32) {
        // Exception - full register dump
        kprintf("=== EXCEPTION #%d ===\n", frame->vector);
        kprintf("RIP=0x%llx  CS=0x%llx  RFLAGS=0x%llx\n", frame->rip, frame->cs, frame->rflags);
        kprintf("RSP=0x%llx  SS=0x%llx\n", frame->rsp, frame->ss);
        kprintf("RAX=0x%llx  RBX=0x%llx  RCX=0x%llx  RDX=0x%llx\n", frame->rax, frame->rbx, frame->rcx, frame->rdx);
        kprintf("RSI=0x%llx  RDI=0x%llx  RBP=0x%llx\n", frame->rsi, frame->rdi, frame->rbp);
        kprintf("R8=0x%llx   R9=0x%llx   R10=0x%llx  R11=0x%llx\n", frame->r8, frame->r9, frame->r10, frame->r11);
        kprintf("R12=0x%llx  R13=0x%llx  R14=0x%llx  R15=0x%llx\n", frame->r12, frame->r13, frame->r14, frame->r15);
        kprintf("Error code: 0x%llx\n", frame->error_code);
        
        // Page fault specific info
        if (frame->vector == 14) {
            u64 cr2;
            asm volatile("mov %%cr2,%0":"=r"(cr2));
            kprintf("Page fault at CR2=0x%llx\n", cr2);
            kprintf("Reason: %s %s %s\n",
                frame->error_code & 1 ? "protection" : "not-present",
                frame->error_code & 2 ? "write" : "read",
                frame->error_code & 4 ? "user" : "kernel");

            // Handle page fault: if not present, allocate page
            if (!(frame->error_code & 1)) {
                u64 phys = pmm_alloc_page();
                if (phys && vmm_map_page(cr2 & ~0xFFF, phys, PTE_WRITABLE | (frame->error_code & 4 ? PTE_USER : 0))) {
                    kprintf("Allocated page for 0x%llx\n", cr2 & ~0xFFF);
                    return; // Continue
                }
            }
            kprintf("Unhandled page fault\n");
        }
        
        // Print task info
        task_t* t = scheduler_current();
        if (t) {
            kprintf("In task: %s (PID %d)\n", t->name, t->pid);
        }
        
        // Check for stack canary
        if (t && t->stack_canary_addr) {
            u64 canary = *(u64*)t->stack_canary_addr;
            if (canary != 0xDEADC0DEDEADC0DE) {
                kprintf("STACK SMASH DETECTED in task %s PID %d - canary was 0x%llx\n", 
                    t->name, t->pid, canary);
                t->state = TASK_STATE_ZOMBIE;
                scheduler_schedule();
                return;
            }
        }
        
        while(1) asm volatile("hlt");
    }

    if (frame->vector >= 32 && frame->vector < 48) {
        u8 irq = frame->vector - 32;
        irq_dispatch(irq, frame);
        // Single EOI path - now handled via pic_send_eoi in irq.c
    }
}
