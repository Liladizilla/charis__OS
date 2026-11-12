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
    /* This fallback may run before the PIT is initialized. TSC and the
     * pool address provide only a weak canary seed, never token entropy. */
    entropy_add(read_tsc());
    entropy_add((u64)(uintptr_t)&entropy_pool);
    return entropy_pool;
}

/* RDRAND is optional on x86-64. Timestamps are not cryptographic entropy:
 * if the CPU lacks RDRAND or it repeatedly fails, token generation fails. */
static bool security_random_u64(u64* value) {
    if (!value) return false;

    u32 eax = 1, ebx = 0, ecx = 0, edx = 0;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (!(ecx & (1U << 30))) return false;

    for (u32 attempt = 0; attempt < 10; attempt++) {
        u8 ready = 0;
        u64 candidate = 0;
        __asm__ volatile("rdrand %0; setc %1"
                         : "=r"(candidate), "=qm"(ready));
        if (ready) {
            *value = candidate;
            return true;
        }
    }
    return false;
}

/* Prefer hardware entropy for the stack guard. The fallback preserves boot
 * compatibility but is explicitly not suitable for security tokens. */
uintptr_t __stack_chk_guard;

__attribute__((no_stack_protector))
void security_init(void) {
    for (int i = 0; i < TASK_MAX_TASKS; i++) {
        security_contexts[i].process_caps = 0;
        security_contexts[i].parent_token = 0;
        security_contexts[i].is_privileged = false;
    }

    u64 seed = 0;
    if (security_random_u64(&seed)) {
        __stack_chk_guard = (uintptr_t)seed;
        entropy_pool = seed;
        vga_puts("Security: hardware entropy available\n");
    } else {
        __stack_chk_guard = (uintptr_t)entropy_get();
        vga_puts("SECURITY WARNING: weak stack-canary seed; RDRAND unavailable\n");
        serial_puts("SECURITY WARNING: RDRAND unavailable; cryptographic tokens disabled\n");
    }
    vga_puts("Security: Framework initialized\n");
}

bool security_check_capability(task_t* task, u32 cap) {
    /* Capability requests are conjunctions: every requested authority must
     * be present. Empty and undefined masks fail closed. */
    if (!task || !cap || (cap & ~CAP_ALL)) return false;
    return (task->capabilities & cap) == cap;
}

void security_enforce_capabilities(task_t* task) {
    if (!task) return;
    /* Strip bits the current kernel does not define before the task runs. */
    task->capabilities &= CAP_ALL;
}

void security_audit(const char* action, task_t* task) {
    task_t* t = task ? task : scheduler_current();
    kprintf("[AUDIT] %s by PID %d (%s) at %llu\n", 
            action, t ? t->pid : 0, t ? t->name : "none", timer_get_ms());
}

bool security_verify_path(const char* path, task_t* task) {
    (void)task;
    if (!path || path[0] != '/') return false;

    /* VFS/FAT paths are slash-separated and bounded by the syscall buffer.
     * Reject alternate separators and parent components before any resolver
     * or filesystem code sees the path. */
    usize length = 0;
    const char* component = path + 1;
    for (const char* p = path + 1; ; p++) {
        if (*p == '\\') return false;
        if (*p && ++length > 255) return false;
        if (*p == '/' || *p == '\0') {
            usize component_length = (usize)(p - component);
            if (component_length == 2 &&
                component[0] == '.' && component[1] == '.') return false;
            if (*p == '\0') break;
            component = p + 1;
        }
    }
    return true;
}

int security_generate_token(u8* out_token) {
    if (!out_token) return -1;

    u64 first = 0;
    for (int offset = 0; offset < SECURITY_TOKEN_SIZE; offset += 8) {
        u64 random_word = 0;
        if (!security_random_u64(&random_word)) {
            /* Never return a partial or predictable token. */
            kmemset(out_token, 0, SECURITY_TOKEN_SIZE);
            return -1;
        }
        if (offset == 0) first = random_word;
        for (u32 byte = 0; byte < 8; byte++) {
            out_token[offset + byte] = (u8)(random_word >> (byte * 8));
        }
    }
    return (int)(u32)first;
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