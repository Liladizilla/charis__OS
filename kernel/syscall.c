#include <kernel/syscall.h>
#include <kernel/scheduler.h>
#include <kernel/vga.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <kernel/vmm.h>
#include <kernel/keyboard.h>
#include <kernel/vfs.h>

static syscall_handler_t syscall_table[SYSCALL_MAX];

static u64 syscall_read_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    int fd = (int)a1;
    void* buf = (void*)a2;
    usize count = (usize)a3;
    
    fd_entry_t* f = fd_get(fd);
    if (!f || !f->node) return -1;
    
    // Handle stdin specially
    if (fd == FD_STDIN) {
        char c;
        if (keyboard_get_key(&c)) {
            *(char*)buf = c;
            return 1;
        }
        return 0;
    }
    
    // Use VFS read
    if (f->node->read) {
        return f->node->read(f->node, f->offset, count, (u8*)buf);
    }
    return -1;
}

static u64 syscall_write_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    int fd = (int)a1;
    const void* buf = (const void*)a2;
    usize count = (usize)a3;
    
    fd_entry_t* f = fd_get(fd);
    if (!f || !f->node) return -1;
    
    // Handle stdout/stderr via VGA
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        const char* s = (const char*)buf;
        for (usize i = 0; i < count && s[i]; i++) {
            vga_putc(s[i]);
        }
        return count;
    }
    
    // Use VFS write
    if (f->node->write) {
        return f->node->write(f->node, f->offset, count, (const u8*)buf);
    }
    return -1;
}

static u64 syscall_exit_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_exit();
    return 0;
}

static u64 syscall_yield_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    scheduler_yield();
    return 0;
}

static u64 syscall_print_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1) return 0;
    char buf[256];
    if ((u64)a1 >= 0xFFFF800000000000ULL) return 0;
    const char* s = (const char*)a1;
    usize i = 0;
    for (; i < sizeof(buf)-1 && s[i]; i++) buf[i] = s[i];
    buf[i] = 0;
    vga_puts(buf);
    return 0;
}

static u64 syscall_getpid_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t* t = scheduler_current();
    return t ? t->pid : 0;
}

static u64 syscall_fork_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    
    task_t* parent = scheduler_current();
    if (!parent) return -1;
    
    pml4_t* child_pml4 = vmm_create_address_space();
    if (!child_pml4) return -1;
    
    vmm_copy_kernel_mappings(child_pml4, parent->mm.pml4);
    
    void* child_stack = kmalloc(TASK_STACK_SIZE + PAGE_SIZE);
    if (!child_stack) return -1;
    
    task_t* child = task_allocate();
    if (!child) {
        kfree(child_stack);
        return -1;
    }
    
    kmemset(child, 0, sizeof(task_t));
    child->pid = task_next_pid();
    child->state = TASK_STATE_READY;
    child->priority = parent->priority;
    child->capabilities = parent->capabilities;
    child->is_user = parent->is_user;
    child->mm.pml4 = child_pml4;
    child->stack_base = (u64)child_stack + PAGE_SIZE;
    child->guard_page_addr = (u64)child_stack;
    child->runtime_ticks = 0;
    child->remaining_quantum = TASK_DEFAULT_QUANTUM;
    
    kstrncpy(child->name, parent->name, TASK_NAME_MAX - 1);
    
    u64* stack_top = (u64*)((u8*)child->stack_base + TASK_STACK_SIZE);
    stack_top = (u64*)((u64)stack_top & ~0xFUL);
    
    *--stack_top = 0x23;
    *--stack_top = child->stack_base + TASK_STACK_SIZE;
    *--stack_top = 0x202;
    *--stack_top = 0x1B;
    *--stack_top = (u64)task_exit_handler;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    *--stack_top = 0;
    
    child->rsp = (u64)stack_top;
    child->stack_canary_addr = child->stack_base + sizeof(u64);
    *(u64*)child->stack_canary_addr = 0xDEADC0DEDEADC0DEULL;
    
    scheduler_add_task(child);
    
    return child->pid;
}

static u64 syscall_sleep_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_sleep_ms((u32)a1);
    return 0;
}

static u64 syscall_open_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1) return -1;
    
    char path[256];
    if ((u64)a1 >= 0xFFFF800000000000ULL) return -1;
    const char* path_str = (const char*)a1;
    usize i = 0;
    for (; i < sizeof(path)-1 && path_str[i]; i++) path[i] = path_str[i];
    path[i] = 0;
    
    vfs_node_t* node = vfs_resolve(path);
    if (!node) return -1;
    
    return fd_alloc(node, 0);
}

static u64 syscall_exec_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1) return -1;
    
    char path[256];
    const char* path_str = (const char*)a1;
    usize i = 0;
    for (; i < sizeof(path)-1 && path_str[i]; i++) path[i] = path_str[i];
    path[i] = 0;
    
    task_t* task = scheduler_current();
    if (!task) return -1;
    
    u64 entry_point;
    if (elf_load(path, &task->mm, &entry_point) < 0) {
        return -1;
    }
    
    // Set up new stack for the process
    u64 stack_top;
    elf_setup_stack(&task->mm, &stack_top, NULL, 0, NULL, 0);
    
    // Build iretq frame for new entry point
    u64* stack_ptr = (u64*)(stack_top - 64); // Leave some room
    
    *--stack_ptr = 0x23;              // SS
    *--stack_ptr = stack_top;         // RSP
    *--stack_ptr = 0x202;             // RFLAGS
    *--stack_ptr = 0x1B;              // CS
    *--stack_ptr = entry_point;         // RIP
    
    task->user_rsp = (u64)stack_ptr;
    task->is_user = true;
    
    return 0; // Success - won't actually return in real exec
}

void syscall_init(void) {
    for (u32 i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = NULL;
    }
    syscall_register(SYS_READ, syscall_read_handler);
    syscall_register(SYS_WRITE, syscall_write_handler);
    syscall_register(SYS_EXIT, syscall_exit_handler);
    syscall_register(SYS_YIELD, syscall_yield_handler);
    syscall_register(SYS_PRINT, syscall_print_handler);
    syscall_register(SYS_GETPID, syscall_getpid_handler);
    syscall_register(SYS_SLEEP, syscall_sleep_handler);
    syscall_register(SYS_FORK, syscall_fork_handler);
    syscall_register(SYS_OPEN, syscall_open_handler);
    syscall_register(SYS_CLOSE, syscall_close_handler);
    syscall_register(SYS_EXEC, syscall_exec_handler);

    extern void syscall_entry(void);
    asm volatile("wrmsr" : : "a"(syscall_entry), "d"(0), "c"(0xC0000080 + 0));
    asm volatile("wrmsr" : : "a"(0), "d"(0), "c"(0xC0000080 + 3));
    asm volatile("wrmsr" : : "a"((0x18ULL << 32) | (0x08ULL << 48)), "d"(0), "c"(0xC0000080 + 4));
    
    idt_set_gate(0x80, isr_table[0x80], 0, 0xEE);
}

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    if (num >= SYSCALL_MAX || !syscall_table[num]) {
        vga_puts("Bad syscall\n");
        return (u64)-1;
    }
    return syscall_table[num](a1, a2, a3, a4, a5, a6);
}

void syscall_register(u64 num, syscall_handler_t handler) {
    if (num < SYSCALL_MAX) {
        syscall_table[num] = handler;
    }
}