/* task.h - Process and thread management */
#pragma once
#include <kernel/types.h>
#include <kernel/idt.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>

#define TASK_STATE_READY        0
#define TASK_STATE_RUNNING      1
#define TASK_STATE_BLOCKED      2
#define TASK_STATE_ZOMBIE       3
#define TASK_STATE_UNUSED       4 /* Free task-table slot; distinct from an exited process. */
#define TASK_STATE_ALLOCATING   5 /* Reserved atomically while a new task is built. */
#define TASK_STATE_EXITING      6 /* Reject new descriptor grants during teardown. */

#define TASK_NAME_MAX           32
#define TASK_STACK_SIZE         8192
#define TASK_MAX_TASKS          32
/* Three-page slots are shared by kernel task stacks and the emergency IST1
 * stack: one unmapped guard page followed by two supervisor-only stack pages. */
#define TASK_STACK_ARENA_BASE    0xFFFF900000400000ULL
#define TASK_STACK_SLOT_PAGES    3U
#define TASK_USER_CODE_MAX_PAGES 4
#define TASK_DEFAULT_QUANTUM    10
#define TASK_WAITING_SLEEP       0x534C4550U /* 'SLEP': blocked until event_data deadline */
#define TASK_WAITING_KEYBOARD    0x4B455942U /* 'KEYB': waiting for keyboard input */
#define TASK_WAITING_CHILD       0x43484C44U /* 'CHLD': waiting for a child exit */
#define IPC_MAX_ENDPOINTS        16U

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
struct ipc_endpoint;

typedef struct task {
    u64 rsp;
    u64 stack_base;
    u32 state;
    u32 pid;
    u32 exit_code; /* Status supplied to SYS_EXIT; retained while zombie. */
    u32 parent_pid; /* PID that created this task, or 0 for kernel bootstrap tasks. */
    u32 slot_index; /* Stable array index; PID is monotonic and not bounded by TASK_MAX_TASKS. */
    u32 priority;
    char name[TASK_NAME_MAX];
    struct task* next;
    struct task* prev;
    u64 runtime_ticks;
    u32 remaining_quantum;
    u32 capabilities;
    u64 guard_page_addr; /* Reserved virtual page; intentionally not present. */
    u64 stack_canary_addr;
    /* Two supervisor-only physical frames backing this task's kernel stack. */
    u64 kernel_stack_pages[2];
    u64 event_data;
    u32 waiting_event;
    bool is_user;
    bool started;
    bool scheduler_queued; /* Membership in the scheduler's intrusive task list. */
    u64 user_stack_base;
    u64 user_rsp;
    /* Dedicated zeroed physical pages mapped only into this task as user stack. */
    u64 user_stack_pages[2];
    /* Private executable copies of the page-aligned bootstrap user text. */
    u64 user_code_pages[TASK_USER_CODE_MAX_PAGES];
    u32 user_code_page_count;
    process_mm_t mm;
    fd_entry_t fd_table[MAX_FDS];
    void* address_space;
    struct ipc_endpoint* ipc_endpoints[IPC_MAX_ENDPOINTS];
    u32 ipc_endpoint_generations[IPC_MAX_ENDPOINTS];
} task_t;

task_t* task_allocate(void);
u32 task_next_pid(void);
task_t* task_find_by_pid(u32 pid);

typedef void (*task_func_t)(void* arg);

void scheduler_init(void);
void scheduler_tick(reg_frame_t* frame);
void scheduler_add_task(task_t* task);
void scheduler_remove_task(task_t* task);
task_t* scheduler_current(void);
void scheduler_yield(void);

void task_init(void);
task_t* task_create(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user);
task_t* task_create_with_pml4(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user, pml4_t* pml4);
void task_discard_unstarted(task_t* task);
/* Point the CPU's privilege-transition stack at this task's private kernel stack. */
bool task_set_tss_rsp0(task_t* task);
task_t* task_allocate(void);
u32 task_next_pid(void);
void task_exit(void);
void task_exit_with_status(u32 status);
/* Wait for an owned child; selector is a PID or -1 for any child. */
s64 task_waitpid(s64 selector, u32* exit_status, bool nohang);

/* Park a task's stack for the scheduler to reclaim once it has switched away
 * from it. A task must not kfree() the stack it is running on. */
void task_defer_release(task_t* t);

/* Reclaim a parked stack. Must be called from a different task's stack --
 * the scheduler does this immediately after a context switch. */
bool task_release_pending(void);
void task_block(task_t* task);
void task_unblock(task_t* task);
void task_sleep_ms(u32 ms);
void task_wake_sleepers(u64 now_ms);
bool task_prepare_user_space(task_t* task, void* entry_point);

/* Assembly context switch */
void context_switch(u64* old_rsp, u64 new_rsp, bool is_user);
void task_trampoline(void);

/* Task exit handler */
void task_exit_handler(void);

/* Wake keyboard readers from IRQ1 without allocating memory in interrupt context. */
void task_wake_keyboard_waiters(void);