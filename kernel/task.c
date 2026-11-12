#include <kernel/task.h>
#include <kernel/task_id.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/timer.h>
#include <kernel/vmm.h>
#include <kernel/security.h>
#include <kernel/signal.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/serial.h>

static task_t task_pool[TASK_MAX_TASKS];
static task_t* task_list = NULL;
static u64 next_pid = 1;

static void task_release_user_stack_pages(task_t* task, pml4_t* pml4);
static void task_release_user_code_pages(task_t* task, pml4_t* pml4);
static bool task_stack_allocate(task_t* task);
static bool task_stack_release(task_t* task);

static u64 task_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void task_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
}

/*
 * Kernel task stacks live in a dedicated page-aligned virtual arena immediately
 * after the 4 MiB kernel heap. Each fixed task slot owns three pages:
 *
 *   [unmapped guard][writable + NX stack page 0][writable + NX stack page 1]
 *
 * The heap's high-half PML4 entry is shared into process address spaces, so
 * these supervisor mappings remain available after a ring-3 CR3 switch. The
 * physical stack frames are owned by the task and are never heap allocations.
 */
static bool task_stack_allocate(task_t* task) {
    if (!task || task->slot_index >= TASK_MAX_TASKS) return false;

    u64 flags = task_irq_save();
    u64 slot_base = TASK_STACK_ARENA_BASE +
                    (u64)task->slot_index * TASK_STACK_SLOT_PAGES * PAGE_SIZE;
    u64 stack_base = slot_base + PAGE_SIZE;

    /* Refuse to overwrite any mapping, including a stale guard or stack page. */
    for (u32 page = 0; page < TASK_STACK_SLOT_PAGES; page++) {
        if (vmm_get_phys(slot_base + (u64)page * PAGE_SIZE) != 0) {
            task_irq_restore(flags);
            return false;
        }
    }

    task->guard_page_addr = slot_base;
    task->stack_base = stack_base;
    task->kernel_stack_pages[0] = 0;
    task->kernel_stack_pages[1] = 0;

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
        task->kernel_stack_pages[i] = phys;
    }

    /* Runtime page-table verification: the guard has no translation and both
     * stack leaves are supervisor-only, writable, NX, and point at owned RAM. */
    {
        u64 cr3;
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        pml4_t* active = (pml4_t*)(uintptr_t)(cr3 & ~0xFFFULL);
        u64 physical = 0, map_flags = 0;
        if (vmm_translate(active, slot_base, &physical, &map_flags)) goto fail;
        for (u32 i = 0; i < 2; i++) {
            if (!vmm_translate(active, stack_base + (u64)i * PAGE_SIZE,
                               &physical, &map_flags) ||
                (physical & ~(PAGE_SIZE - 1)) != task->kernel_stack_pages[i] ||
                !(map_flags & VMM_FLAG_WRITABLE) ||
                (map_flags & VMM_FLAG_USER) ||
                !(map_flags & VMM_FLAG_NX)) goto fail;
        }
    }

    task->stack_canary_addr = stack_base + sizeof(u64);
    *(u64*)(uintptr_t)task->stack_canary_addr = 0xDEADC0DEDEADC0DEULL;
    serial_puts("TASK_STACK_GUARD_OK\n");
    task_irq_restore(flags);
    return true;

fail:
    if (!task_stack_release(task)) {
        serial_puts("FATAL: kernel stack allocation rollback failed\n");
    }
    task_irq_restore(flags);
    return false;
}

/* Called only while on another stack, or before this task has been queued. */
static bool task_stack_release(task_t* task) {
    if (!task) return false;
    u64 flags = task_irq_save();

    for (u32 i = 0; i < 2; i++) {
        u64 phys = task->kernel_stack_pages[i];
        if (!phys) continue;
        u64 virt = task->stack_base + (u64)i * PAGE_SIZE;
        u64 unmapped = vmm_unmap_page_current(virt);
        if (unmapped != phys) {
            serial_puts("FATAL: kernel stack mapping ownership mismatch\n");
            task_irq_restore(flags);
            return false;
        }
        pmm_free_page(unmapped);
        task->kernel_stack_pages[i] = 0;
    }

    task->stack_base = 0;
    task->guard_page_addr = 0;
    task->stack_canary_addr = 0;
    task_irq_restore(flags);
    return true;
}


void task_init(void) {
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        task_pool[i].rsp = 0;
        task_pool[i].stack_base = 0;
        task_pool[i].state = TASK_STATE_UNUSED;
        task_pool[i].scheduler_queued = false;
        task_pool[i].pid = 0;
        task_pool[i].exit_code = 0;
        task_pool[i].parent_pid = 0;
        task_pool[i].slot_index = i;
        task_pool[i].priority = 0;
        task_pool[i].next = NULL;
        task_pool[i].prev = NULL;
        task_pool[i].runtime_ticks = 0;
        task_pool[i].remaining_quantum = TASK_DEFAULT_QUANTUM;
        task_pool[i].capabilities = 0;
        task_pool[i].guard_page_addr = 0;
        task_pool[i].stack_canary_addr = 0;
        task_pool[i].kernel_stack_pages[0] = 0;
        task_pool[i].kernel_stack_pages[1] = 0;
        task_pool[i].event_data = 0;
        task_pool[i].waiting_event = 0;
        task_pool[i].mm.pml4 = NULL;
        task_pool[i].mm.heap_start = 0;
        task_pool[i].mm.heap_end = 0;
        task_pool[i].mm.stack_top = 0;
        task_pool[i].mm.vmas = NULL;
        task_pool[i].user_stack_base = 0;
        task_pool[i].user_rsp = 0;
        task_pool[i].user_stack_pages[0] = 0;
        task_pool[i].user_stack_pages[1] = 0;
        task_pool[i].user_code_page_count = 0;
        for (u32 page = 0; page < TASK_USER_CODE_MAX_PAGES; page++)
            task_pool[i].user_code_pages[page] = 0;

        // Initialize fd table
        for (int j = 0; j < MAX_FDS; j++) {
            task_pool[i].fd_table[j].file = NULL;
            task_pool[i].fd_table[j].rights = 0;
            task_pool[i].fd_table[j].generation = 0;
        }
        fd_table_init(task_pool[i].fd_table);
    }
    task_list = NULL;
}

extern void task_trampoline(void);

task_t* task_allocate(void) {
    /* Reserve the slot before restoring interrupts. Otherwise a timer
     * preemption could let two creators receive the same UNUSED task. */
    u64 flags = task_irq_save();
    task_t* allocated = NULL;
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_STATE_UNUSED) {
            task_pool[i].state = TASK_STATE_ALLOCATING;
            allocated = &task_pool[i];
            break;
        }
    }
    task_irq_restore(flags);
    return allocated;
}

u32 task_next_pid(void) {
    return task_pid_allocate(&next_pid);
}

/* Search the fixed task table by PID. Zombie records remain discoverable until
 * a future wait/reap path explicitly releases them. */
task_t* task_find_by_pid(u32 pid) {
    if (!pid) return NULL;
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        if (task_pool[i].state != TASK_STATE_UNUSED && task_pool[i].pid == pid) {
            return &task_pool[i];
        }
    }
    return NULL;
}

/*
 * Deferred stack release.
 *
 * A task cannot free the stack it is standing on. kfree() hands the pages back
 * to the allocator while the return address, the frame pointer and every local
 * of task_exit_handler() still live on them -- the corruption is invisible
 * until something reuses that memory. Both task_exit() and the trampoline's
 * task_exit_handler() did exactly that.
 *
 * So the exit path parks the pointer here and the scheduler reclaims it once
 * it has switched to a different task and is provably no longer on those
 * pages. One deep is enough: a task cannot reach this point twice, because the
 * scheduler only ever picks TASK_STATE_READY tasks, so a task parked here is
 * never resumed and never parks another.
 */
static task_t* g_pending_task = NULL;

void task_defer_release(task_t* t) {
    if (!t) return;
    if (g_pending_task && g_pending_task != t) {
        serial_puts("FATAL: more than one deferred task stack\n");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }
    if (t->kernel_stack_pages[0] || t->kernel_stack_pages[1])
        g_pending_task = t;
    t->state = TASK_STATE_ZOMBIE;
    t->runtime_ticks = 0;
}

/* Called by the scheduler from a different task's stack. */
bool task_release_pending(void) {
    task_t* task = g_pending_task;
    if (!task) return false;

    if (!task_stack_release(task)) {
        serial_puts("FATAL: unable to reclaim exited task stack\n");
        return false;
    }
    g_pending_task = NULL;
    /* The task_t remains as an unreaped zombie record for future waitpid(). */
    task->state = TASK_STATE_ZOMBIE;
    return true;
}

void task_exit_handler(void) {
    task_exit_with_status(0);
    /* Unreachable: the task is a zombie and the scheduler only picks READY
     * tasks, so it is never scheduled again. Kept so that a scheduler bug
     * halts here rather than returning into freed memory. */
    while (1) asm volatile("hlt");
}

void task_exit_with_status(u32 status) {
    task_t* current = scheduler_current();
    if (current) {
        current->exit_code = status;
        /* Publish a terminal state before releasing descriptors. A kernel
         * descriptor duplication racing teardown must never publish into a
         * task after its close-all pass has completed. */
        current->state = TASK_STATE_EXITING;
        /* Release per-task descriptor state before the task becomes a zombie.
         * The table slot remains available for waitpid(), but open descriptors
         * must not remain live for the lifetime of the zombie record. */
        fd_table_close_all(current->fd_table);
        if (current->is_user && current->mm.pml4) {
            /* Stop using the process CR3 before reclaiming any of its private
             * page tables or USER frames. The kernel identity map remains
             * available on the saved kernel CR3 while this exit path runs. */
            pml4_t* dead_space = current->mm.pml4;
            vmm_switch(NULL);
            vmm_destroy_address_space(dead_space);
            current->address_space = NULL;
            current->mm.pml4 = NULL;
            current->mm.heap_start = 0;
            current->mm.heap_end = 0;
            current->mm.stack_top = 0;
            current->mm.vmas = NULL;
            current->user_stack_base = 0;
            current->user_rsp = 0;
            current->user_stack_pages[0] = 0;
            current->user_stack_pages[1] = 0;
            current->user_code_page_count = 0;
            for (u32 page = 0; page < TASK_USER_CODE_MAX_PAGES; page++)
                current->user_code_pages[page] = 0;
            serial_puts("RING3_PRIVATE_PAGES_RELEASED\n");
        }
        /* Exited tasks are never scheduled again. Unlink the zombie before
         * parking its stack so the scheduler list only contains live tasks. */
        scheduler_remove_task(current);
    }
    task_defer_release(current);
    scheduler_yield();

    /*
     * If no runnable task exists, scheduler_yield() can return without a
     * context switch. Returning from SYS_EXIT would then resume the exited
     * task (possibly at ring 3) even though its state is ZOMBIE and its stack
     * has been deferred for release. Exiting is terminal: if the scheduler
     * cannot switch away, wait for interrupts forever rather than executing
     * another instruction in the dead task.
     */
    for (;;) {
        asm volatile("sti; hlt" : : : "memory");
    }
}

void task_exit(void) {
    task_exit_with_status(0);
}

void task_block(task_t* task) {
    if (task) {
        task->state = TASK_STATE_BLOCKED;
    }
}

/* A blocked task may wake itself from STI;HLT while it is still the
 * current CPU context. In that case it is already executing and must return
 * to RUNNING, not remain marked READY while consuming the CPU. */
static void task_mark_runnable(task_t* task) {
    if (!task) return;
    task->state = (task == scheduler_current())
                ? TASK_STATE_RUNNING : TASK_STATE_READY;
}

void task_unblock(task_t* task) {
    if (task && task->state == TASK_STATE_BLOCKED) {
        task_mark_runnable(task);
    }
}

task_t* task_current(void) {
    return scheduler_current();
}

// Common task initialization helper
static bool task_init_common(task_t* task, const char* name, u32 capabilities, bool is_user) {
    /* PID exhaustion must never recycle an identity used in an old handle. */
    u32 pid = task_next_pid();
    if (!pid) return false;

    task->is_user = is_user;
    task->started = false;
    task->scheduler_queued = false;
    task->pid = pid;
    task->exit_code = 0;
    task_t* parent = scheduler_current();
    task->parent_pid = parent ? parent->pid : 0;
    signal_reset_task(task);
    task->state = TASK_STATE_READY;
    task->priority = 0;
    task->runtime_ticks = 0;
    task->remaining_quantum = TASK_DEFAULT_QUANTUM;
    task->capabilities = capabilities;
    security_enforce_capabilities(task);
    task->mm.pml4 = NULL;
    task->mm.heap_start = 0;
    task->mm.heap_end = 0;
    task->mm.stack_top = 0;
    task->mm.vmas = NULL;
    task->user_stack_base = 0;
    task->user_rsp = 0;
    task->user_stack_pages[0] = 0;
    task->user_stack_pages[1] = 0;
    task->user_code_page_count = 0;
    for (u32 page = 0; page < TASK_USER_CODE_MAX_PAGES; page++)
        task->user_code_pages[page] = 0;
    task->event_data = 0;
    task->waiting_event = 0;
    task->ipc_wait_next = NULL;

    /* Drop any residual descriptors before this task slot receives a new
     * identity. Generations survive so stale tokens cannot become valid. */
    fd_table_close_all(task->fd_table);

    for (u32 i = 0; i < TASK_NAME_MAX - 1 && name && name[i]; i++) {
        task->name[i] = name[i];
    }
    task->name[TASK_NAME_MAX - 1] = '\0';

    task->next = NULL;
    task->prev = NULL;
    task->stack_canary_addr = task->stack_base + sizeof(u64);
    *(u64*)task->stack_canary_addr = 0xDEADC0DEDEADC0DEULL;
    return true;
}

static void task_release_user_stack_pages(task_t* task, pml4_t* pml4) {
    if (!task) return;
    const u64 top = 0x00007FFFFFFFE000ULL;
    const u64 start = top - (2 * PAGE_SIZE);

    for (u32 i = 0; i < 2; i++) {
        u64 phys = task->user_stack_pages[i];
        if (!phys) continue;

        /* Remove the user mapping before returning its owned frame to PMM. */
        u64 mapped = pml4 ? vmm_unmap_page_pml4(pml4, start + (i * PAGE_SIZE)) : 0;
        pmm_free_page(mapped ? mapped : phys);
        task->user_stack_pages[i] = 0;
    }
    task->user_stack_base = 0;
    task->user_rsp = 0;
}

/* Release only the private physical frames owned by this process's bootstrap
 * user-code mapping. The kernel's original text pages are never freed. */
static void task_release_user_code_pages(task_t* task, pml4_t* pml4) {
    if (!task) return;
    extern char _usertext_start[];
    u64 start = (u64)(uintptr_t)_usertext_start;
    for (u32 i = 0; i < TASK_USER_CODE_MAX_PAGES; i++) {
        u64 phys = task->user_code_pages[i];
        if (!phys) continue;
        u64 mapped = pml4 ? vmm_unmap_page_pml4(pml4, start + ((u64)i * PAGE_SIZE)) : 0;
        pmm_free_page(mapped ? mapped : phys);
        task->user_code_pages[i] = 0;
    }
    task->user_code_page_count = 0;
}

bool task_prepare_user_space(task_t* task, void* entry_point) {
    if (!task || !task->address_space || !entry_point) return false;

    extern char _usertext_start[];
    extern char _usertext_end[];
    pml4_t* pml4 = (pml4_t*)task->address_space;
    u64 code_start = (u64)(uintptr_t)_usertext_start;
    u64 code_end = (u64)(uintptr_t)_usertext_end;
    u64 entry = (u64)(uintptr_t)entry_point;

    /* The entry must live inside the dedicated linker section. Never grant
     * USER access to the kernel's shared .text/.rodata pages. */
    if (!code_start || code_end <= code_start ||
        (code_start & (PAGE_SIZE - 1)) || (code_end & (PAGE_SIZE - 1)) ||
        entry < code_start || entry >= code_end) return false;

    u64 code_pages = (code_end - code_start) / PAGE_SIZE;
    if (!code_pages || code_pages > TASK_USER_CODE_MAX_PAGES) return false;

    task_release_user_code_pages(task, pml4);
    for (u32 i = 0; i < (u32)code_pages; i++) {
        u64 phys = pmm_alloc_page();
        if (!phys) {
            task_release_user_code_pages(task, pml4);
            return false;
        }

        void* alias = vmm_phys_to_virt(phys);
        if (!alias) {
            pmm_free_page(phys);
            task_release_user_code_pages(task, pml4);
            return false;
        }

        /* The dedicated .usertext section contains only the bootstrap entry.
         * Copy it into a zeroed private frame so adjacent kernel code/data is
         * never disclosed by a user-readable identity-mapped page. */
        kmemset(alias, 0, PAGE_SIZE);
        const u8* source = (const u8*)(uintptr_t)(code_start + ((u64)i * PAGE_SIZE));
        u8* destination = (u8*)alias;
        for (u32 byte = 0; byte < PAGE_SIZE; byte++) destination[byte] = source[byte];

        u64 user_page = code_start + ((u64)i * PAGE_SIZE);
        if (!vmm_map_page_pml4(pml4, user_page, phys,
                VMM_FLAG_PRESENT | VMM_FLAG_USER)) {
            pmm_free_page(phys);
            task_release_user_code_pages(task, pml4);
            return false;
        }
        task->user_code_pages[i] = phys;
        task->user_code_page_count++;

        u64 mapped_phys = 0, mapped_flags = 0;
        if (!vmm_translate(pml4, user_page, &mapped_phys, &mapped_flags) ||
            (mapped_phys & ~(PAGE_SIZE - 1)) != phys ||
            !(mapped_flags & VMM_FLAG_USER) ||
            (mapped_flags & VMM_FLAG_WRITABLE) ||
            (mapped_flags & VMM_FLAG_NX)) {
            task_release_user_code_pages(task, pml4);
            return false;
        }
    }

    /* Give ring 3 two dedicated, zeroed, writable/NX stack pages. */
    const u64 user_stack_top = 0x00007FFFFFFFE000ULL;
    const u64 user_stack_start = user_stack_top - (2 * PAGE_SIZE);
    for (u32 i = 0; i < 2; i++) {
        u64 phys = pmm_alloc_page();
        if (!phys) {
            task_release_user_stack_pages(task, pml4);
            task_release_user_code_pages(task, pml4);
            return false;
        }
        task->user_stack_pages[i] = phys;
        void* page_alias = vmm_phys_to_virt(phys);
        if (!page_alias) {
            task->user_stack_pages[i] = 0;
            pmm_free_page(phys);
            task_release_user_stack_pages(task, pml4);
            task_release_user_code_pages(task, pml4);
            return false;
        }
        kmemset(page_alias, 0, PAGE_SIZE);

        if (!vmm_map_page_pml4(pml4, user_stack_start + ((u64)i * PAGE_SIZE), phys,
                VMM_FLAG_PRESENT | VMM_FLAG_USER | VMM_FLAG_WRITABLE | VMM_FLAG_NX)) {
            task_release_user_stack_pages(task, pml4);
            task_release_user_code_pages(task, pml4);
            return false;
        }
    }

    task->user_stack_base = user_stack_start;
    task->user_rsp = user_stack_top;
    task->mm.stack_top = user_stack_top;
    ((u64*)task->rsp)[10] = user_stack_top;
    task->mm.pml4 = pml4;
    serial_puts("RING3_CODE_ISOLATION_OK\n");
    return true;
}

task_t* task_create(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user) {
    task_t* task = task_allocate();
    if (!task) {
        vga_puts("Failed to allocate task\n");
        return NULL;
    }

    if (!task_stack_allocate(task)) {
        vga_puts("Failed to allocate guarded task stack\n");
        task_discard_unstarted(task);
        return NULL;
    }

    if (!task_init_common(task, name, capabilities, is_user)) {
        vga_puts("Task ID space exhausted\n");
        task_discard_unstarted(task);
        return NULL;
    }

    // Create per-process address space (BUG-11 fix)
    if (is_user) {
        task->address_space = vmm_create_address_space();
        if (!task->address_space) {
            vga_puts("Failed to create address space\n");
            /* A failed constructor releases its private stack mappings and
             * returns the reserved task slot to the pool. */
            task_discard_unstarted(task);
            return NULL;
        }
    } else {
        task->address_space = NULL; // Kernel tasks share boot page table
    }

    u64* stack_top = (u64*)((u8*)task->stack_base + TASK_STACK_SIZE);
    stack_top = (u64*)((u64)stack_top & ~0xFUL);

    if (is_user) {
        task->user_stack_base = task->stack_base;
        task->user_rsp = (u64)stack_top;
        *--stack_top = 0x23;
        *--stack_top = task->user_rsp;
        *--stack_top = 0x202;
        *--stack_top = 0x1B;
        *--stack_top = (u64)func;
        /* context_switch restores six callee-saved registers, then skips one
         * synthetic kernel-RFLAGS slot before iretq consumes the five-word
         * ring-3 frame (RIP, CS, RFLAGS, RSP, SS). Keep this slot separate from
         * the user RFLAGS word: the latter is restored only by iretq. */
        *--stack_top = 0x2; /* synthetic kernel RFLAGS; IF stays clear */
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    } else {
        /* Layout must match the pops in context_switch, which take
         * r15,r14,r13,r12,rbx,rbp and then `ret`. The return address has to
         * be task_trampoline, not task_exit_handler: trampoline pops func and
         * arg off this same stack and calls them. Returning into
         * task_exit_handler instead skipped the entry function entirely and
         * kfree'd the stack we were executing on, so the kernel took a triple
         * fault the instant it entered the scheduler. */
        *--stack_top = (u64)arg;
        *--stack_top = (u64)func;
        *--stack_top = (u64)task_trampoline;
        *--stack_top = 0x2; /* synthetic kernel RFLAGS, restored by context_switch */
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    }

    task->rsp = (u64)stack_top;
    return task;
}

task_t* task_create_with_pml4(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user, pml4_t* pml4) {
    /* A ring-3 task must always own a valid, page-aligned address-space root.
     * Kernel tasks use the shared kernel CR3 and must not receive a private
     * process root through this constructor. */
    if ((is_user && (!pml4 || ((u64)(uintptr_t)pml4 & (PAGE_SIZE - 1)))) ||
        (!is_user && pml4)) return NULL;

    task_t* task = task_allocate();
    if (!task) {
        vga_puts("Failed to allocate task\n");
        return NULL;
    }

    if (!task_stack_allocate(task)) {
        vga_puts("Failed to allocate guarded task stack\n");
        task_discard_unstarted(task);
        return NULL;
    }

    if (!task_init_common(task, name, capabilities, is_user)) {
        vga_puts("Task ID space exhausted\n");
        task_discard_unstarted(task);
        return NULL;
    }
    task->mm.pml4 = pml4;
    task->address_space = pml4; // Use provided PML4

    u64* stack_top = (u64*)((u8*)task->stack_base + TASK_STACK_SIZE);
    stack_top = (u64*)((u64)stack_top & ~0xFUL);

    if (is_user) {
        task->user_stack_base = task->stack_base;
        task->user_rsp = (u64)stack_top;
        *--stack_top = 0x23;
        *--stack_top = task->user_rsp;
        *--stack_top = 0x202;
        *--stack_top = 0x1B;
        *--stack_top = (u64)func;
        /* Keep the synthetic kernel-RFLAGS slot between the six saved
         * registers and the five-word ring-3 iret frame. */
        *--stack_top = 0x2; /* synthetic kernel RFLAGS; IF stays clear */
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    } else {
        /* Layout must match the pops in context_switch, which take
         * r15,r14,r13,r12,rbx,rbp and then `ret`. The return address has to
         * be task_trampoline, not task_exit_handler: trampoline pops func and
         * arg off this same stack and calls them. Returning into
         * task_exit_handler instead skipped the entry function entirely and
         * kfree'd the stack we were executing on, so the kernel took a triple
         * fault the instant it entered the scheduler. */
        *--stack_top = (u64)arg;
        *--stack_top = (u64)func;
        *--stack_top = (u64)task_trampoline;
        *--stack_top = 0x2; /* synthetic kernel RFLAGS, restored by context_switch */
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    }

    task->rsp = (u64)stack_top;
    return task;
}

/* The CPU reads TSS.RSP0 on every CPL3 -> CPL0 transition. A single
 * boot-stack value is unsafe once more than one user task exists. */
extern u8 tss[];

bool task_set_tss_rsp0(task_t* task) {
    if (!task || !task->stack_base) return false;
    u64 stack_top = task->stack_base + TASK_STACK_SIZE;
    u64 observed = 0;
    /* RSP0 is intentionally unaligned at TSS byte offset 4. Use explicit
     * x86 moves so the compiler cannot assume natural u64 alignment. */
    asm volatile("movq %0, 4(%1)"
                 :
                 : "r"(stack_top), "r"(tss)
                 : "memory");
    asm volatile("movq 4(%1), %0"
                 : "=r"(observed)
                 : "r"(tss)
                 : "memory");
    return observed == stack_top;
}

void task_discard_unstarted(task_t* task) {
    if (!task || task->state == TASK_STATE_UNUSED ||
        task->started || task->scheduler_queued) return;

    if (!task_stack_release(task)) return;
    task->state = TASK_STATE_UNUSED;
    task->pid = 0;
    task->parent_pid = 0;
    task->exit_code = 0;
    task->rsp = 0;
    task->stack_base = 0;
    task->guard_page_addr = 0;
    task->stack_canary_addr = 0;
    task->kernel_stack_pages[0] = 0;
    task->kernel_stack_pages[1] = 0;
    task->address_space = NULL;
    task->is_user = false;
    task->started = false;
    task->capabilities = 0;
    task->user_stack_base = 0;
    task->user_rsp = 0;
    task->user_stack_pages[0] = 0;
    task->user_stack_pages[1] = 0;
    task->user_code_page_count = 0;
    for (u32 page = 0; page < TASK_USER_CODE_MAX_PAGES; page++)
        task->user_code_pages[page] = 0;
    task->mm.pml4 = NULL;
    task->mm.heap_start = 0;
    task->mm.heap_end = 0;
    task->mm.stack_top = 0;
    task->mm.vmas = NULL;
    task->event_data = 0;
    task->waiting_event = 0;
    task->next = NULL;
    task->prev = NULL;
    task->ipc_wait_next = NULL;
    task->name[0] = '\0';
    fd_table_close_all(task->fd_table);
}

void task_sleep_ms(u32 ms) {
    if (ms == 0) return;

    /* A syscall arrives through an interrupt gate with IF cleared. Preserve
     * the caller's full flags: the wait loop briefly enables interrupts so
     * HLT can wake, but must not leak IF=1 back into the syscall dispatcher. */
    u64 saved_flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(saved_flags) : : "memory");

    task_t* current = scheduler_current();
    if (!current) {
        /* Early boot or a non-task caller: there is no task to park. Keep the
         * same interrupt-safe wait semantics without relying on timer_sleep_ms
         * being called with IF already enabled. */
        u64 start = timer_get_ms();
        while (timer_get_ms() - start < (u64)ms) {
            asm volatile("sti; hlt; cli" : : : "memory");
        }
        asm volatile("pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
        return;
    }

    /*
     * Mark this blocked reason explicitly. event_data is also used by other
     * event paths, so the timer must not wake arbitrary blocked tasks just
     * because their event_data happens to look like a deadline.
     *
     * Keep IF clear until the blocked state is visible and the scheduler has
     * had the chance to switch away. That closes the deadline-publication race.
     */
    current->event_data = timer_get_ms() + (u64)ms;
    current->waiting_event = TASK_WAITING_SLEEP;
    current->state = TASK_STATE_BLOCKED;
    scheduler_yield();

    /* scheduler_yield() only switches when another task is runnable. With a
     * single task (or when every peer is blocked), it returns immediately.
     * Do not let a blocked sleeper continue executing just because there was
     * nobody else to run. STI;HLT;CLI sleeps atomically with respect to the
     * next interrupt, then returns to the saved IF state before checking again. */
    while (current->state == TASK_STATE_BLOCKED &&
           current->waiting_event == TASK_WAITING_SLEEP) {
        asm volatile("sti; hlt; cli" : : : "memory");
    }

    asm volatile("pushq %0; popfq" : : "r"(saved_flags) : "memory", "cc");
}

void task_wake_sleepers(u64 now_ms) {
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        task_t* task = &task_pool[i];
        if (task->state != TASK_STATE_BLOCKED ||
            task->waiting_event != TASK_WAITING_SLEEP) {
            continue;
        }

        if (now_ms >= task->event_data) {
            task->waiting_event = 0;
            task->event_data = 0;
            task_mark_runnable(task);
        }
    }
}

/* IRQ1 cannot allocate or free queue nodes. Keyboard waiters live in the
 * fixed task table, so waking them is bounded and allocation-free. */
void task_wake_keyboard_waiters(void) {
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        task_t* task = &task_pool[i];
        if (task->state == TASK_STATE_BLOCKED &&
            task->waiting_event == TASK_WAITING_KEYBOARD) {
            task->waiting_event = 0;
            task_mark_runnable(task);
        }
    }
}
