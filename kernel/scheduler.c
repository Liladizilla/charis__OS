#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/signal.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/diagnostics.h>

static task_t* ready_head = NULL;
static task_t* ready_tail = NULL;
task_t* current_task = NULL;

extern void context_switch(u64* old_rsp, u64 new_rsp, bool is_user);

void spinlock_init(spinlock_t* lock) {
    lock->lock = 0;
}

void spinlock_lock(spinlock_t* lock) {
    while (__sync_lock_test_and_set(&lock->lock, 1)) {
        asm volatile("pause");
    }
}

void spinlock_unlock(spinlock_t* lock) {
    __sync_lock_release(&lock->lock);
}

/* Scheduler list mutations can also be reached from ordinary kernel
 * code while the PIT is enabled. Keep IRQ0 from observing a half-linked node.
 * This is local-CPU exclusion; the current kernel is not SMP-enabled. */
static inline u64 scheduler_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void scheduler_irq_restore(u64 flags) {
    asm volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

static void enqueue_task(task_t* task) {
    task->next = NULL;
    task->prev = ready_tail;
    if (ready_tail) {
        ready_tail->next = task;
    } else {
        ready_head = task;
    }
    ready_tail = task;
    task->scheduler_queued = true;
}

void scheduler_init(void) {
    ready_head = NULL;
    ready_tail = NULL;
    current_task = NULL;
}

void scheduler_add_task(task_t* task) {
    if (!task) return;

    u64 flags = scheduler_irq_save();
    if (task->state == TASK_STATE_UNUSED ||
        task->state == TASK_STATE_ALLOCATING ||
        task->state == TASK_STATE_EXITING ||
        task->state == TASK_STATE_ZOMBIE) {
        scheduler_irq_restore(flags);
        return;
    }

    /* A task is linked once for its lifetime. Re-adding an existing node used
     * to overwrite its next/prev pointers and could orphan the ready list or
     * create a self-loop when the node was already the tail. */
    if (!task->scheduler_queued) {
        task->state = TASK_STATE_READY;
        enqueue_task(task);
    }
    scheduler_irq_restore(flags);
}

void scheduler_remove_task(task_t* task) {
    if (!task) return;

    u64 flags = scheduler_irq_save();
    if (!task->scheduler_queued) {
        scheduler_irq_restore(flags);
        return;
    }

    if (task->prev) task->prev->next = task->next;
    else ready_head = task->next;

    if (task->next) task->next->prev = task->prev;
    else ready_tail = task->prev;

    task->next = NULL;
    task->prev = NULL;
    task->scheduler_queued = false;
    scheduler_irq_restore(flags);
}

static task_t* scheduler_find_next(void) {
    if (!ready_head) return NULL;

    // Start from current task's next, or head if no current
    task_t* iter = current_task ? current_task->next : ready_head;
    if (!iter) iter = ready_head;

    task_t* first = iter;
    do {
        if (iter->state == TASK_STATE_READY) {
            return iter;
        }
        iter = iter->next ? iter->next : ready_head;
    } while (iter && iter != first);

    return NULL;
}

void scheduler_block_task(task_t* task) {
    if (!task) return;
    task->state = TASK_STATE_BLOCKED;
    signal_dispatch(task); /* Check for signals while blocked */
}

void scheduler_unblock_task(task_t* task) {
    if (!task || task->state != TASK_STATE_BLOCKED) return;
    /* Blocked tasks remain linked in the scheduler's task list; the scheduler
     * skips them by state. Appending the same node again corrupts the doubly
     * linked list (and can create a self-loop when it is already the tail).
     * task_unblock also preserves the RUNNING invariant when this is the
     * current task waking from its own HLT path. */
    task_unblock(task);
}

void scheduler_start(void) {
    /* Do not let PIT preempt the bootstrap handoff after current_task is
     * published but before the CPU has moved onto that task's stack. */
    u64 flags = scheduler_irq_save();
    if (!ready_head) {
        scheduler_irq_restore(flags);
        vga_puts("No tasks to schedule\n");
        return;
    }

    task_t* next = ready_head;
    current_task = next;
    current_task->state = TASK_STATE_RUNNING;

    /* Every privilege transition must land on the selected task's own kernel
     * stack, never the single bootstrap stack left in TSS.RSP0 by assembly. */
    bool use_iret = current_task->is_user && !current_task->started;
    if (!task_set_tss_rsp0(current_task)) {
        serial_puts("FATAL: unable to install task TSS.RSP0\n");
        for (;;) asm volatile("cli; hlt");
    }
    if (use_iret) serial_puts("TSS_RSP0_USER_STACK_OK\n");

    /* The first task switch must also load that task's address space. */
    if (current_task->address_space) {
        vmm_switch((pml4_t*)current_task->address_space);
    }

    current_task->started = true;
    diag_note_context_switch();
    u64 bootstrap_rsp = 0;
    context_switch(&bootstrap_rsp, current_task->rsp, use_iret);

    /* The bootstrap stack is not normally scheduled again, but keep the
     * original interrupt state if a future scheduler path ever returns here. */
    scheduler_irq_restore(flags);
}

void scheduler_schedule(void) {
    /* Keep the ready-list walk, current_task publication, CR3 update and stack
     * handoff atomic with respect to IRQ0. The saved IF bit is restored on the
     * task's own stack after its context_switch call eventually resumes. */
    u64 flags = scheduler_irq_save();
    task_t* next = scheduler_find_next();
    if (!next) {
        scheduler_irq_restore(flags);
        return;
    }
    if (next == current_task) {
        /* A wakeup can make the currently executing task READY while it is
         * returning from HLT. If no peer is runnable, selecting that same
         * task must restore the RUNNING invariant rather than leaving the
         * live CPU context marked READY. */
        if (current_task && current_task->state == TASK_STATE_READY) {
            current_task->state = TASK_STATE_RUNNING;
        }
        scheduler_irq_restore(flags);
        return;
    }

    task_t* previous = current_task;
    if (previous && previous->state == TASK_STATE_RUNNING) {
        previous->state = TASK_STATE_READY;
    }

    current_task = next;
    current_task->state = TASK_STATE_RUNNING;

    bool use_iret = current_task->is_user && !current_task->started;
    if (!task_set_tss_rsp0(current_task)) {
        serial_puts("FATAL: unable to install task TSS.RSP0\n");
        for (;;) asm volatile("cli; hlt");
    }
    if (use_iret) serial_puts("TSS_RSP0_USER_STACK_OK\n");

    // Switch address space if needed (BUG-11 fix)
    if (previous && previous->address_space != current_task->address_space) {
        vmm_switch((pml4_t*)current_task->address_space);
    }

    current_task->started = true;
    diag_note_context_switch();
    if (previous) {
        context_switch(&previous->rsp, current_task->rsp, use_iret);
    } else {
        context_switch(NULL, current_task->rsp, use_iret);
    }

    /* context_switch() reclaimed any deferred outgoing stack on the
     * destination stack, including first-run trampoline and iretq entries. */
    scheduler_irq_restore(flags);
}

void scheduler_yield(void) {
    if (!current_task) return;
    scheduler_schedule();
}

task_t* scheduler_current(void) {
    return current_task;
}