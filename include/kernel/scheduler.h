/* scheduler.h - Cooperative round-robin scheduler */
#pragma once
#include <kernel/types.h>
#include <kernel/task.h>
#include <kernel/idt.h>

/* Simple spinlock for scheduler synchronization */
typedef struct {
    volatile u64 lock;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_lock(spinlock_t* lock);
void spinlock_unlock(spinlock_t* lock);

/* Scheduler functions */
void scheduler_init(void);
void scheduler_add_task(task_t* task);
void scheduler_start(void);
void scheduler_schedule(void);
void scheduler_yield(void);
task_t* scheduler_current(void);
