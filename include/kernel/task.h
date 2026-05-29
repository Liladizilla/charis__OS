/* task.h - Process and thread management */
#pragma once
#include <kernel/types.h>
#include <kernel/idt.h>
#include <kernel/vfs.h>

#define TASK_STATE_READY        0
#define TASK_STATE_RUNNING      1
#define TASK_STATE_BLOCKED      2
#define TASK_STATE_ZOMBIE       3

#define TASK_NAME_MAX           32
#define TASK_STACK_SIZE         8192
#define TASK_MAX_TASKS          32
#define TASK_DEFAULT_QUANTUM    10

/* VMA - Virtual Memory Area */
typedef struct vma {
    u64 start;
    u64 end;
    u32 flags;
    struct vma* next;
} vma_t;

/* Process memory management */
typedef struct {
    pml4_t* pml4;
    u64 heap_start;
    u64 heap_end;
    u64 stack_top;
    vma_t* vmas;
} process_mm_t;

/* Task control block */
typedef struct task {
    u64 rsp;
    u64 stack_base;
    u32 state;
    u32 pid;
    u32 priority;
    char name[TASK_NAME_MAX];
    struct task* next;
    struct task* prev;
    u64 runtime_ticks;
    u32 remaining_quantum;
    u32 capabilities;
    u64 guard_page_addr;
    u64 stack_canary_addr;
    u64 event_data;
    u32 waiting_event;
    bool is_user;
    u64 user_stack_base;
    u64 user_rsp;
    process_mm_t mm;
    fd_entry_t fd_table[MAX_FDS]; // Per-process file descriptor table
} task_t;

typedef void (*task_func_t)(void* arg);

/* Capability flags */
#define CAP_FS_READ    (1<<0)
#define CAP_FS_WRITE   (1<<1)
#define CAP_FS_CREATE  (1<<2)
#define CAP_FS_DELETE  (1<<3)
#define CAP_SPAWN      (1<<4)
#define CAP_KILL       (1<<5)
#define CAP_RAW_MEM    (1<<6)
#define CAP_SHUTDOWN   (1<<7)
#define CAP_SERIAL     (1<<8)
#define CAP_ALL        0xFFFFFFFF

void scheduler_init(void);
void scheduler_tick(reg_frame_t* frame);
void scheduler_add_task(task_t* task);
void scheduler_remove_task(task_t* task);
task_t* scheduler_current(void);
void scheduler_yield(void);

void task_init(void);
task_t* task_create(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user);
task_t* task_create_with_pml4(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user, pml4_t* pml4);
void task_exit(void);
void task_block(task_t* task);
void task_unblock(task_t* task);
void task_sleep_ms(u32 ms);

/* Assembly context switch */
void context_switch(u64* old_rsp, u64 new_rsp, bool is_user);
void task_trampoline(void);

/* Task exit handler */
void task_exit_handler(void);

/* Task wait queue */
typedef struct wait_queue {
    struct task* task;
    struct wait_queue* next;
} wait_queue_t;

void wait_queue_init(void);
void wait_queue_add(struct task* task);
void wait_queue_wake(void);