#include <kernel/syscall.h>
#include <kernel/scheduler.h>
#include <kernel/vga.h>
#include <kernel/idt.h>

static syscall_handler_t syscall_table[SYSCALL_MAX];

static u64 syscall_yield_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    scheduler_yield();
    return 0;
}

static u64 syscall_print_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    if (a1) {
        vga_puts((const char*)a1);
    }
    return 0;
}

void syscall_init(void) {
    for (u32 i = 0; i < SYSCALL_MAX; i++) {
        syscall_table[i] = NULL;
    }
    syscall_register(SYS_YIELD, syscall_yield_handler);
    syscall_register(SYS_PRINT, syscall_print_handler);
    // Register the system call gate in the IDT (vector 0x80, DPL=3)
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

u64 sys_print(const char* str) {
    return syscall_dispatch(SYS_PRINT, (u64)str, 0, 0, 0, 0, 0);
}
