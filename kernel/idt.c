#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/printf.h>
#include <kernel/memory.h>

#define IDT_ENTRIES 256
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;
static u8 page_fault_ist_stack[8192] __attribute__((aligned(16)));

extern u64 isr_table[256];
extern u8 tss[];

/* Install IST1 before the IDT is loaded. The TSS's IST1 field is at byte
 * offset 36; like RSP0 at offset 4, it is not naturally aligned. */
static bool idt_configure_page_fault_ist(void) {
    u64 stack_top = (u64)(uintptr_t)(page_fault_ist_stack +
                                     sizeof(page_fault_ist_stack));
    u64 observed = 0;
    asm volatile("movq %0, 36(%1)"
                 :
                 : "r"(stack_top), "r"(tss)
                 : "memory");
    asm volatile("movq 36(%1), %0"
                 : "=r"(observed)
                 : "r"(tss)
                 : "memory");
    return observed == stack_top && (stack_top & 0xFULL) == 0;
}

static bool idt_page_fault_frame_on_ist(const reg_frame_t* frame) {
    u64 start = (u64)(uintptr_t)page_fault_ist_stack;
    u64 end = start + sizeof(page_fault_ist_stack);
    u64 address = (u64)(uintptr_t)frame;
    return address >= start && address <= end &&
           sizeof(*frame) <= end - address;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base = (u64)&idt;

    // Clear IDT
    int i;
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* A kernel-stack overflow must not push its exception frame onto the
     * stack that just faulted. Reserve IST1 as a dedicated page-fault landing
     * stack and fail closed if the TSS field cannot be installed. */
    if (!idt_configure_page_fault_ist()) {
        serial_puts("FATAL: unable to configure page-fault IST1\n");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }

    // Load ISR stubs from assembly
    for (i = 0; i < 256; i++) {
        if (isr_table[i]) {
            idt_set_gate(i, isr_table[i], i == 14 ? 1 : 0, 0x8E);
        }
    }
    serial_puts("PAGE_FAULT_IST_READY\n");

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
        if (frame->rax == SYS_EXEC && (frame->cs & 3) == 3) {
            if (!syscall_exec_from_frame(frame)) frame->rax = (u64)-1;
            return;
        }
        // System call
        frame->rax = syscall_dispatch(frame->rax, frame->rdi, frame->rsi, frame->rdx, frame->r10, frame->r8, frame->r9);
        return;
    }

    if (frame->vector < 32) {
        task_t* t = scheduler_current();

        kprintf("=== EXCEPTION #%d ===\n", frame->vector);
        kprintf("RIP=0x%llx  CS=0x%llx  RFLAGS=0x%llx\n",
                frame->rip, frame->cs, frame->rflags);
        kprintf("RSP=0x%llx  SS=0x%llx\n", frame->rsp, frame->ss);
        kprintf("RAX=0x%llx  RBX=0x%llx  RCX=0x%llx  RDX=0x%llx\n",
                frame->rax, frame->rbx, frame->rcx, frame->rdx);
        kprintf("RSI=0x%llx  RDI=0x%llx  RBP=0x%llx\n",
                frame->rsi, frame->rdi, frame->rbp);
        kprintf("R8=0x%llx   R9=0x%llx   R10=0x%llx  R11=0x%llx\n",
                frame->r8, frame->r9, frame->r10, frame->r11);
        kprintf("R12=0x%llx  R13=0x%llx  R14=0x%llx  R15=0x%llx\n",
                frame->r12, frame->r13, frame->r14, frame->r15);
        kprintf("Error code: 0x%llx\n", frame->error_code);

        u64 cr2 = 0;
        if (frame->vector == 14) {
            asm volatile("mov %%cr2,%0" : "=r"(cr2));
            if (!idt_page_fault_frame_on_ist(frame)) {
                serial_puts("PAGE_FAULT_IST_FRAME_INVALID\n");
            } else {
                serial_puts("PAGE_FAULT_IST_FRAME_OK\n");
            }
            kprintf("Page fault at CR2=0x%llx\n", cr2);
            kprintf("Reason: %s %s %s\n",
                frame->error_code & 1 ? "protection" : "not-present",
                frame->error_code & 2 ? "write" : "read",
                frame->error_code & 4 ? "user" : "kernel");
        }

        /* User faults are process failures, not kernel failures. There is no
         * VMA registration/lazy-page policy yet, so never allocate a page just
         * because an arbitrary address faulted. */
        if ((frame->cs & 3) == 3 && t && t->is_user) {
            if (frame->vector == 14) {
                kprintf("USER PAGE FAULT: PID %d task %s CR2=0x%llx\n",
                        t->pid, t->name, cr2);
            } else {
                kprintf("USER EXCEPTION: PID %d task %s vector %d RIP=0x%llx\n",
                        t->pid, t->name, frame->vector, frame->rip);
            }
            task_exit_with_status(128 + (u32)frame->vector);
            for (;;) asm volatile("cli; hlt" : : : "memory");
        }

        if (frame->vector == 14 && t && !t->is_user &&
            t->guard_page_addr &&
            cr2 >= t->guard_page_addr &&
            cr2 < t->guard_page_addr + 4096ULL &&
            !(frame->error_code & 1ULL) &&
            (frame->error_code & 2ULL) &&
            !(frame->error_code & 4ULL) &&
            idt_page_fault_frame_on_ist(frame)) {
            serial_puts("KERNEL_STACK_GUARD_FAULT_CONTAINED\n");
#ifdef TEST_KERNEL_STACK_GUARD
            serial_puts("KERNEL_STACK_GUARD_TEST_PASS\n");
#endif
        }

        if (t && t->stack_canary_addr) {
            u64 canary = *(u64*)t->stack_canary_addr;
            if (canary != 0xDEADC0DEDEADC0DE) {
                kprintf("STACK SMASH DETECTED in task %s PID %d - canary was 0x%llx\n",
                        t->name, t->pid, canary);
            }
        }

        if (frame->vector == 14) {
            kprintf("KERNEL PANIC: unhandled page fault at CR2=0x%llx\n", cr2);
        } else {
            kprintf("KERNEL PANIC: unhandled exception #%d\n", frame->vector);
        }
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }

    if (frame->vector >= 32 && frame->vector < 48) {
        u8 irq = frame->vector - 32;
        irq_dispatch(irq, frame);
        // Single EOI path - now handled via pic_send_eoi in irq.c
    }
}
