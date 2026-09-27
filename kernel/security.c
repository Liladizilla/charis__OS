#include <kernel/security.h>
#include <kernel/task.h>
#include <kernel/vga.h>
#include <kernel/timer.h>
#include <kernel/printf.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/io.h>

static security_context_t security_contexts[TASK_MAX_TASKS];

static u64 entropy_pool = 0;

static u64 read_tsc(void) {
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

static void entropy_add(u64 val) {
    entropy_pool ^= val;
    entropy_pool *= 0x9E3779B97F4A7C15ULL;
}

static u64 entropy_get(void) {
    entropy_add(read_tsc());
    entropy_add(timer_get_ms());
    entropy_add((u64)(uintptr_t)&entropy_pool);
    return entropy_pool;
}

/* Stack canary guard value - randomized at boot */
uintptr_t __stack_chk_guard;

void security_init(void) {
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        security_contexts[i].process_caps = 0;
        security_contexts[i].parent_token = 0;
        security_contexts[i].is_privileged = false;
    }
    __stack_chk_guard = (uintptr_t)entropy_get();
    vga_puts("Security: Framework initialized\n");
}

bool security_check_capability(task_t* task, u32 cap) {
    if (!task) return false;
    return (task->capabilities & cap) != 0;
}

void security_enforce_capabilities(task_t* task) {
    if (!task) return;
    task->capabilities &= CAP_ALL;
}

void security_audit(const char* action, task_t* task) {
    task_t* t = task ? task : scheduler_current();
    kprintf("[AUDIT] %s by PID %d (%s) at %llu\n", 
            action, t ? t->pid : 0, t ? t->name : "none", timer_get_ms());
}

bool security_verify_path(const char* path, task_t* task) {
    (void)task;
    if (!path) return false;
    
    for (const char* p = path; *p; p++) {
        if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0' || p[2] == 0)) {
            return false;
        }
    }
    
    return true;
}

int security_generate_token(u8* out_token) {
    u32 token = (u32)(entropy_get() & 0xFFFFFFFF);
    if (out_token) {
        for (int i = 0; i < SECURITY_TOKEN_SIZE; i++) {
            out_token[i] = (u8)(token ^ (entropy_get() & 0xFF) ^ (i * 0x5A));
        }
    }
    return token;
}

/* Called by GCC when a stack canary mismatch is detected */
__attribute__((noreturn))
void __stack_chk_fail(void) {
    serial_puts("\n[KERNEL PANIC] Stack smashing detected!\n");
    serial_puts("A buffer overflow corrupted a stack canary.\n");
    serial_puts("System halted.\n");
    
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}