#include <kernel/task.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/scheduler.h>
#include <kernel/timer.h>

static task_t task_pool[TASK_MAX_TASKS];
static task_t* task_list = NULL;
static u32 next_pid = 1;

/* Wait queue for tasks blocked on keyboard input */
static wait_queue_t* wait_queue_head = NULL;
static wait_queue_t* wait_queue_tail = NULL;

void task_init(void) {
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        task_pool[i].rsp = 0;
        task_pool[i].stack_base = 0;
        task_pool[i].state = TASK_STATE_ZOMBIE;
        task_pool[i].pid = 0;
        task_pool[i].priority = 0;
        task_pool[i].next = NULL;
        task_pool[i].prev = NULL;
        task_pool[i].runtime_ticks = 0;
        task_pool[i].remaining_quantum = TASK_DEFAULT_QUANTUM;
        task_pool[i].capabilities = 0;
        task_pool[i].guard_page_addr = 0;
        task_pool[i].stack_canary_addr = 0;
        task_pool[i].event_data = 0;
        task_pool[i].waiting_event = 0;
        task_pool[i].mm.pml4 = NULL;
        task_pool[i].mm.heap_start = 0;
        task_pool[i].mm.heap_end = 0;
        task_pool[i].mm.stack_top = 0;
        task_pool[i].mm.vmas = NULL;
        
        // Initialize fd table
        for (int j = 0; j < MAX_FDS; j++) {
            task_pool[i].fd_table[j].node = NULL;
            task_pool[i].fd_table[j].offset = 0;
            task_pool[i].fd_table[j].flags = 0;
        }
        // stdin, stdout, stderr point to keyboard and vga devices
        extern vfs_node_t dev_kbd;
        extern vfs_node_t dev_vga;
        task_pool[i].fd_table[FD_STDIN].node = &dev_kbd;
        task_pool[i].fd_table[FD_STDOUT].node = &dev_vga;
        task_pool[i].fd_table[FD_STDERR].node = &dev_vga;
    }
    task_list = NULL;
}

static task_t* task_allocate(void) {
    for (u32 i = 0; i < TASK_MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_STATE_ZOMBIE) {
            return &task_pool[i];
        }
    }
    return NULL;
}

u32 task_next_pid(void) {
    return next_pid++;
}

void task_exit_handler(void) {
    task_t* current = scheduler_current();
    if (current) {
        /* Free the task's stack memory */
        if (current->stack_base) {
            kfree((void*)current->stack_base);
        }
        
        current->state = TASK_STATE_ZOMBIE;
        current->runtime_ticks = 0;
    }
    scheduler_yield();
    while (1) {
        asm volatile("hlt");
    }
}

void task_exit(void) {
    task_t* current = scheduler_current();
    if (current) {
        /* Free the task's stack memory */
        if (current->stack_base) {
            kfree((void*)current->stack_base);
            current->stack_base = 0;
        }
        
        current->state = TASK_STATE_ZOMBIE;
    }
    scheduler_yield();
}

void task_block(task_t* task) {
    if (task) {
        task->state = TASK_STATE_BLOCKED;
    }
}

void task_unblock(task_t* task) {
    if (task && task->state == TASK_STATE_BLOCKED) {
        task->state = TASK_STATE_READY;
    }
}

task_t* task_current(void) {
    return scheduler_current();
}

task_t* task_create(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user) {
    task_t* task = task_allocate();
    if (!task) {
        vga_puts("Failed to allocate task\n");
        return NULL;
    }

    void* stack = kmalloc(TASK_STACK_SIZE + PAGE_SIZE);
    if (!stack) {
        vga_puts("Failed to allocate task stack\n");
        return NULL;
    }

    task->stack_base = (u64)stack + PAGE_SIZE;
    task->guard_page_addr = (u64)stack;
    task->is_user = is_user;
    task->pid = next_pid++;
    task->state = TASK_STATE_READY;
    task->priority = 0;
    task->runtime_ticks = 0;
    task->remaining_quantum = TASK_DEFAULT_QUANTUM;
    task->capabilities = capabilities;
    task->mm.pml4 = NULL;
    task->mm.heap_start = 0;
    task->mm.heap_end = 0;
    task->mm.stack_top = 0;
    task->mm.vmas = NULL;
    task->event_data = 0;
    task->waiting_event = 0;

    for (u32 i = 0; i < TASK_NAME_MAX - 1 && name && name[i]; i++) {
        task->name[i] = name[i];
    }
    task->name[TASK_NAME_MAX - 1] = '\0';

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
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    } else {
        *--stack_top = (u64)arg;
        *--stack_top = (u64)func;
        *--stack_top = (u64)task_exit_handler;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    }

    task->rsp = (u64)stack_top;
    task->next = NULL;
    task->prev = NULL;
    task->stack_canary_addr = task->stack_base + sizeof(u64);
    *(u64*)task->stack_canary_addr = 0xDEADC0DEDEADC0DEULL;

    return task;
}

task_t* task_create_with_pml4(const char* name, task_func_t func, void* arg, u32 capabilities, bool is_user, pml4_t* pml4) {
    task_t* task = task_allocate();
    if (!task) {
        vga_puts("Failed to allocate task\n");
        return NULL;
    }

    void* stack = kmalloc(TASK_STACK_SIZE + PAGE_SIZE);
    if (!stack) {
        vga_puts("Failed to allocate task stack\n");
        return NULL;
    }

    task->stack_base = (u64)stack + PAGE_SIZE;
    task->guard_page_addr = (u64)stack;
    task->is_user = is_user;
    task->pid = next_pid++;
    task->state = TASK_STATE_READY;
    task->priority = 0;
    task->runtime_ticks = 0;
    task->remaining_quantum = TASK_DEFAULT_QUANTUM;
    task->capabilities = capabilities;
    task->mm.pml4 = pml4;
    task->mm.heap_start = 0;
    task->mm.heap_end = 0;
    task->mm.stack_top = 0;
    task->mm.vmas = NULL;
    task->event_data = 0;
    task->waiting_event = 0;

    for (u32 i = 0; i < TASK_NAME_MAX - 1 && name && name[i]; i++) {
        task->name[i] = name[i];
    }
    task->name[TASK_NAME_MAX - 1] = '\0';

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
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    } else {
        *--stack_top = (u64)arg;
        *--stack_top = (u64)func;
        *--stack_top = (u64)task_exit_handler;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
        *--stack_top = 0;
    }

    task->rsp = (u64)stack_top;
    task->next = NULL;
    task->prev = NULL;
    task->stack_canary_addr = task->stack_base + sizeof(u64);
    *(u64*)task->stack_canary_addr = 0xDEADC0DEDEADC0DEULL;

    return task;
}

void task_sleep_ms(u32 ms) {
    task_t* current = scheduler_current();
    if (current) {
        current->state = TASK_STATE_BLOCKED;
        u64 wake_tick = timer_get_ms() + ms;
        current->event_data = wake_tick;
        scheduler_yield();
    }
}

/* Wait queue implementation */
void wait_queue_init(void) {
    wait_queue_head = NULL;
    wait_queue_tail = NULL;
}

void wait_queue_add(struct task* task) {
    wait_queue_t* node = (wait_queue_t*)kmalloc(sizeof(wait_queue_t));
    if (!node) return;
    
    node->task = task;
    node->next = NULL;
    
    if (wait_queue_tail) {
        wait_queue_tail->next = node;
    } else {
        wait_queue_head = node;
    }
    wait_queue_tail = node;
}

void wait_queue_wake(void) {
    if (wait_queue_head) {
        wait_queue_t* node = wait_queue_head;
        wait_queue_head = node->next;
        if (!wait_queue_head) {
            wait_queue_tail = NULL;
        }
        if (node->task && node->task->state == TASK_STATE_BLOCKED) {
            node->task->state = TASK_STATE_READY;
        }
        kfree(node);
    }
}
