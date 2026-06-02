#include <kernel/scheduler.h>
#include <kernel/task.h>
#include <kernel/signal.h>
#include <kernel/vga.h>

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

static void enqueue_task(task_t* task) {
    if (!task) return;
    task->next = NULL;
    task->prev = ready_tail;
    if (ready_tail) {
        ready_tail->next = task;
    } else {
        ready_head = task;
    }
    ready_tail = task;
}

void scheduler_init(void) {
    ready_head = NULL;
    ready_tail = NULL;
    current_task = NULL;
}

void scheduler_add_task(task_t* task) {
    if (!task) return;
    task->state = TASK_STATE_READY;
    enqueue_task(task);
}

static task_t* scheduler_find_next(void) {
    if (!ready_head) return NULL;

    task_t* iter = current_task ? current_task->next : ready_head;
    if (!iter) iter = ready_head;

    task_t* first = iter;
    while (iter) {
        if (iter->state == TASK_STATE_READY) {
            return iter;
        }
        iter = iter->next ? iter->next : ready_head;
        if (iter == first) break;
    }
    return NULL;
}

void scheduler_block_task(task_t* task) {
    if (!task) return;
    task->state = TASK_STATE_BLOCKED;
    signal_dispatch(task); /* Check for signals while blocked */
}

void scheduler_unblock_task(task_t* task) {
    if (!task || task->state != TASK_STATE_BLOCKED) return;
    task->state = TASK_STATE_READY;
    enqueue_task(task);
}

void scheduler_start(void) {
    if (!ready_head) {
        vga_puts("No tasks to schedule\n");
        return;
    }

    task_t* next = ready_head;
    current_task = next;
    current_task->state = TASK_STATE_RUNNING;

    u64 bootstrap_rsp = 0;
    context_switch(&bootstrap_rsp, current_task->rsp, current_task->is_user);
}

void scheduler_schedule(void) {
    task_t* next = scheduler_find_next();
    if (!next || next == current_task) return;

    task_t* previous = current_task;
    if (previous && previous->state == TASK_STATE_RUNNING) {
        previous->state = TASK_STATE_READY;
    }

    current_task = next;
    current_task->state = TASK_STATE_RUNNING;

    if (previous) {
        context_switch(&previous->rsp, current_task->rsp, current_task->is_user);
    } else {
        context_switch(NULL, current_task->rsp, current_task->is_user);
    }
}

void scheduler_yield(void) {
    if (!current_task) return;
    scheduler_schedule();
}

task_t* scheduler_current(void) {
    return current_task;
}