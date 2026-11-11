#include <kernel/idt.h>
#include <kernel/irq.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/syscall.h>
#include <kernel/printf.h>
#include <kernel/memory.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/string.h>

#define IDT_ENTRIES 256
static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

/*
 * IST1 occupies the reserved task-stack arena slot immediately after the
 * TASK_MAX_TASKS kernel-task slots:
 *
 *   [unmapped guard][writable + NX page 0][writable + NX page 1]
 *
 * The arena begins just after the 4 MiB kernel heap. Its high-half PML4 entry
 * is shared into ring-3 address spaces, so page faults from user mode can also
 * land here without exposing the stack to user access.
 */
#define PAGE_FAULT_IST_SLOT (TASK_MAX_TASKS)
static u64 page_fault_ist_guard;
static u64 page_fault_ist_stack_base;
static u64 page_fault_ist_pages[2];

extern u64 isr_table[256];
extern u8 tss[];

static bool idt_release_page_fault_ist(void) {
    bool ok = true;
    for (u32 i = 0; i < 2; i++) {
        u64 owned = page_fault_ist_pages[i];
        if (!owned) continue;
        u64 virt = page_fault_ist_stack_base + (u64)i * PAGE_SIZE;
        u64 unmapped = vmm_unmap_page_current(virt);
        if (unmapped != owned) {
            serial_puts("FATAL: page-fault IST frame ownership mismatch\\n");
            ok = false;
            continue;
        }
        pmm_free_page(unmapped);
        page_fault_ist_pages[i] = 0;
    }
    page_fault_ist_guard = 0;
    page_fault_ist_stack_base = 0;
    return ok;
}

/* Allocate and verify a real supervisor-only, writable, NX emergency stack.
 * The TSS is not pointed at the stack until both leaves and its guard have
 * passed the runtime page-table checks. */
static bool idt_configure_page_fault_ist(void) {
    u64 slot_base = TASK_STACK_ARENA_BASE +
                    (u64)PAGE_FAULT_IST_SLOT * TASK_STACK_SLOT_PAGES * PAGE_SIZE;
    u64 stack_base = slot_base + PAGE_SIZE;
    u64 stack_top = stack_base + 2ULL * PAGE_SIZE;
    u64 cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4_t* active = (pml4_t*)(uintptr_t)(cr3 & ~0xFFFULL);
    u64 physical = 0, map_flags = 0;

    /* Refuse to replace any existing mapping, including a stale guard. */
    for (u32 page = 0; page < TASK_STACK_SLOT_PAGES; page++) {
        if (vmm_translate(active, slot_base + (u64)page * PAGE_SIZE,
                          &physical, &map_flags)) return false;
    }

    page_fault_ist_guard = slot_base;
    page_fault_ist_stack_base = stack_base;

    for (u32 i = 0; i < 2; i++) {
        u64 phys = pmm_alloc_page();
        if (!phys) goto fail;
        void* alias = vmm_phys_to_virt(phys);
        if (!alias) {
            pmm_free_page(phys);
            goto fail;
        }
        kmemset(alias, 0, PAGE_SIZE);

        u64 virt = stack_base + (u64)i * PAGE_SIZE;
        if (!vmm_map_page(virt, phys, VMM_FLAG_WRITABLE | VMM_FLAG_NX)) {
            pmm_free_page(phys);
            goto fail;
        }
        page_fault_ist_pages[i] = phys;
    }

    /* Recheck the absent guard and effective permissions at every level. */
    if (vmm_translate(active, page_fault_ist_guard, &physical, &map_flags))
        goto fail;
    for (u32 i = 0; i < 2; i++) {
        if (!vmm_translate(active, stack_base + (u64)i * PAGE_SIZE,
                           &physical, &map_flags) ||
            (physical & ~(PAGE_SIZE - 1ULL)) != page_fault_ist_pages[i] ||
            !(map_flags & VMM_FLAG_PRESENT) ||
            !(map_flags & VMM_FLAG_WRITABLE) ||
            (map_flags & VMM_FLAG_USER) ||
            !(map_flags & VMM_FLAG_NX)) goto fail;
    }
    if ((stack_top & 0xFULL) != 0) goto fail;

    /* TSS.IST1 is at byte offset 36 and is not naturally aligned. */
    {
        u64 observed = 0;
        asm volatile("movq %0, 36(%1)"
                     :
                     : "r"(stack_top), "r"(tss)
                     : "memory");
        asm volatile("movq 36(%1), %0"
                     : "=r"(observed)
                     : "r"(tss)
                     : "memory");
        if (observed != stack_top) goto fail;
    }

    serial_puts("PAGE_FAULT_IST_GUARD_OK\\n");
    serial_puts("PAGE_FAULT_IST_STACK_PERMISSIONS_OK\\n");
    return true;

fail:
    if (!idt_release_page_fault_ist()) {
        serial_puts("FATAL: unable to roll back page-fault IST mappings\\n");
    }
    return false;
}

static bool idt_page_fault_frame_on_ist(const reg_frame_t* frame) {
    u64 start = page_fault_ist_stack_base;
    u64 end = start + 2ULL * PAGE_SIZE;
    u64 address = (u64)(uintptr_t)frame;
    return start && address >= start && address <= end &&
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
