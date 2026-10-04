#include <kernel/syscall.h>
#include <kernel/scheduler.h>
#include <kernel/vga.h>
#include <kernel/printf.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <kernel/vmm.h>
#include <kernel/keyboard.h>
#include <kernel/vfs.h>
#include <kernel/ipc.h>
#include <kernel/socket.h>
#include <kernel/audio.h>
#include <kernel/hda.h>
#include <kernel/fb.h>
#include <kernel/memory.h>
#include <kernel/diagnostics.h>
#include <kernel/string.h>
#include <kernel/elf.h>
#include <kernel/security.h>

extern u64 isr_table[256];

static syscall_handler_t syscall_table[SYSCALL_MAX];

static u64 syscall_read_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a4; (void)a5; (void)a6;
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
    (void)a4; (void)a5; (void)a6;
    int fd = (int)a1;
    const void* buf = (const void*)a2;
    usize count = (usize)a3;
    
    fd_entry_t* f = fd_get(fd);
    if (!f || !f->node) return -1;
    
    // Handle stdout/stderr via VGA
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        const char* s = (const char*)buf;
        for (usize i = 0; i < count && s[i]; i++) {
            vga_putchar(s[i]);
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
    kprintf("%s", buf);
    return 0;
}

static u64 syscall_getpid_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_t* t = scheduler_current();
    return t ? t->pid : 0;
}

static u64 syscall_fork_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;

    /* fork() is not implemented, and the shape it used to have could not work.
     * Fail cleanly rather than hand back a child that is guaranteed to fault.
     *
     * 1. Real fork() semantics -- the child resumes at the parent's interrupted
     *    RIP with a copied register frame and a fresh stack, differing only in
     *    the return value -- need the parent's frame at the moment of the call.
     *    A syscall handler signature of (a1..a6) does not carry it: the frame
     *    built by interrupt_stubs.asm and by the syscall_entry fast path never
     *    reaches this function. Substituting some other entry point would not
     *    be fork() semantics.
     *
     * 2. What it actually built was worse than wrong. The child's frame was
     *    CS=0x1B, SS=0x23 -- the ring-3 selectors, from gdt64.user_code and
     *    user_data -- with RIP=task_exit_handler, which is kernel code. The
     *    first instruction ran at CPL 3 and hit `hlt`; HLT is privileged above
     *    CPL 0 and raises #GP, so the child died with EXCEPTION #13 the moment
     *    fork() was called.
     *
     *    The same exit path has an independent fault: task_exit_handler()
     *    kfree()s the stack of the task it is currently executing on. A task
     *    that reaches it via task_trampoline after its entry function returns
     *    is still standing on that stack. Freeing it is use-after-free, and it
     *    applies to every exit path, not just this one.
     *
     * This is currently unreachable in practice: only a ring-3 process can
     * issue SYS_FORK, and the ring-3 "user" task is deliberately not enqueued
     * (see main.c). Ring-3 scheduling has its own open gaps listed there: CR3
     * is not loaded on the first switch, TSS.rsp0 is never set,
     * task->address_space is never assigned, and the task stack from kmalloc()
     * is never mapped into the new PML4.
     *
     * TODO: implement once (1) the interrupted frame is threaded through to
     * syscall handlers and (2) ring-3 scheduling works. Build the child stack
     * from the parent's saved RIP/RSP/GPRs so it resumes at the fork() call
     * site, and defer freeing a task's stack until after the scheduler has
     * provably switched away from it.
     */
    return -1;
}

static u64 syscall_sleep_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    task_sleep_ms((u32)a1);
    return 0;
}

static u64 syscall_open_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1) return -1;
    
    task_t* task = scheduler_current();
    if (!task) return -1;
    
    // Capability check: need FS_READ to open files
    if (!security_check_capability(task, CAP_FS_READ)) {
        return -1; // EPERM
    }

    char path[256];
    if ((u64)a1 >= 0xFFFF800000000000ULL) return -1;
    const char* path_str = (const char*)a1;
    usize i = 0;
    for (; i < sizeof(path)-1 && path_str[i]; i++) path[i] = path_str[i];
    path[i] = 0;
    
    vfs_node_t* node = vfs_resolve(path);
    if (!node) return -1;
    
    // Path traversal check
    if (!security_verify_path(path, task)) {
        return -1; // EPERM
    }
    
    return fd_alloc(node, 0);
}

static u64 syscall_close_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    fd_close((int)a1);
    return 0;
}

static u64 syscall_shm_alloc_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return shm_alloc();
}

static u64 syscall_shm_get_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (u64)shm_get((int)a1);
}

static u64 syscall_shm_free_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    shm_free((int)a1);
    return 0;
}

static u64 syscall_ipc_create_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1) return -1;
    char name[32];
    usize i = 0;
    for (; i < sizeof(name)-1 && ((char*)a1)[i]; i++) name[i] = ((char*)a1)[i];
    name[i] = 0;
    return ipc_create(name);
}

static u64 syscall_ipc_send_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a4; (void)a5; (void)a6;
    return ipc_send((int)a1, (void*)a2, a3);
}

static u64 syscall_ipc_recv_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a4; (void)a5; (void)a6;
    return ipc_recv((int)a1, (void*)a2, a3);
}

static u64 syscall_exec_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1) return -1;
    
    task_t* task = scheduler_current();
    if (!task) return -1;
    
    // Capability check: need SPAWN to execute new programs
    if (!security_check_capability(task, CAP_SPAWN)) {
        return -1; // EPERM
    }

    char path[256];
    const char* path_str = (const char*)a1;
    usize i = 0;
    for (; i < sizeof(path)-1 && path_str[i]; i++) path[i] = path_str[i];
    path[i] = 0;
    
    // Path traversal check
    if (!security_verify_path(path, task)) {
        return -1; // EPERM
    }
    
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

static u64 syscall_socket_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return socket_create((int)a1, (int)a2);
}

static u64 syscall_connect_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return socket_connect((int)a1, (u32)a2, (u16)a3);
}

static u64 syscall_bind_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return socket_bind((int)a1, (u16)a2);
}

static u64 syscall_listen_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return socket_listen((int)a1);
}

static u64 syscall_accept_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return socket_accept((int)a1);
}

static u64 syscall_send_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a4; (void)a5; (void)a6;
    return socket_send((int)a1, (void*)a2, a3);
}

static u64 syscall_recv_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a4; (void)a5; (void)a6;
    return socket_recv((int)a1, (void*)a2, a3);
}

static u64 syscall_socket_close_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    socket_close((int)a1);
    return 0;
}

static u64 syscall_beep_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    audio_beep((u32)a1, (u32)a2);
    return 0;
}

static u64 syscall_diag_stats_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    system_stats_t* stats = (system_stats_t*)a1;
    if (stats) diag_collect_stats(stats);
    return 0;
}

static u64 syscall_diag_tasks_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    diag_dump_tasks();
    return 0;
}

/* Game SDK handlers */
static u64 syscall_game_init_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0; /* Placeholder */
}

static u64 syscall_game_clear_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    fb_clear((u32)a1);
    return 0;
}

static u64 syscall_game_flip_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0; /* No-op for single buffer */
}

static u64 syscall_game_audio_open_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    audio_stream_t* s = audio_open((int)a1, 2, 16);
    return s ? (u64)s->buffer : (u64)-1;
}

static u64 syscall_game_audio_play_handler(u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return audio_write((audio_stream_t*)a1, (void*)a2, (usize)a3) >= 0 ? 0 : -1;
}

/* ── Model-specific register helpers ──────────────────────────────────── */
#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084
#define EFER_SCE    (1ULL << 0)      /* System Call Extensions */

/* STAR[47:32] is the SYSCALL code selector, STAR[63:48] the SYSRET base. */
#define STAR_VALUE  ((0x18ULL << 48) | (0x08ULL << 32))

static u64 rdmsr(u32 msr) {
    u32 lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static void wrmsr(u32 msr, u64 value) {
    asm volatile("wrmsr" : : "c"(msr), "a"((u32)value), "d"((u32)(value >> 32)));
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
    syscall_register(SYS_SHM_ALLOC, syscall_shm_alloc_handler);
    syscall_register(SYS_SHM_GET, syscall_shm_get_handler);
    syscall_register(SYS_SHM_FREE, syscall_shm_free_handler);
    syscall_register(SYS_IPC_CREATE, syscall_ipc_create_handler);
    syscall_register(SYS_IPC_SEND, syscall_ipc_send_handler);
    syscall_register(SYS_IPC_RECV, syscall_ipc_recv_handler);
    syscall_register(SYS_SOCKET, syscall_socket_handler);
    syscall_register(SYS_CONNECT, syscall_connect_handler);
    syscall_register(SYS_BIND, syscall_bind_handler);
    syscall_register(SYS_LISTEN, syscall_listen_handler);
    syscall_register(SYS_ACCEPT, syscall_accept_handler);
    syscall_register(SYS_SEND, syscall_send_handler);
    syscall_register(SYS_RECV, syscall_recv_handler);
    syscall_register(SYS_SOCKET_CLOSE, syscall_socket_close_handler);
    syscall_register(SYS_BEEP, syscall_beep_handler);
    syscall_register(SYS_DIAG_STATS, syscall_diag_stats_handler);
    syscall_register(SYS_DIAG_TASKS, syscall_diag_tasks_handler);
    syscall_register(SYS_GAME_INIT, syscall_game_init_handler);
    syscall_register(SYS_GAME_CLEAR, syscall_game_clear_handler);
    syscall_register(SYS_GAME_FLIP, syscall_game_flip_handler);
    syscall_register(SYS_GAME_AUDIO_OPEN, syscall_game_audio_open_handler);
    syscall_register(SYS_GAME_AUDIO_PLAY, syscall_game_audio_play_handler);

    extern void syscall_entry(void);

    /*
     * Program the SYSCALL/SYSRET MSRs.
     *
     * These were all misnumbered. The old code wrote:
     *   0xC0000080 (EFER)     with the entry point as the value
     *   0xC0000083 (reserved)  with zero
     *   0xC0000084 (SFMASK)   with the STAR selector pair
     * Writing reserved EFER bits, or any value to a reserved MSR, raises #GP.
     *
     * That is why this only ever worked under emulation. TCG does not validate
     * EFER's reserved bits and silently accepts writes to unimplemented MSRs,
     * so the misnumbering went unnoticed; a real CPU -- and therefore KVM,
     * which is what GNOME Boxes and every other accelerated VM uses -- faults
     * on the first wrmsr and the kernel resets.
     */
    wrmsr(MSR_EFER,   rdmsr(MSR_EFER) | EFER_SCE);
    wrmsr(MSR_STAR,   STAR_VALUE);
    wrmsr(MSR_LSTAR,  (u64)syscall_entry);
    /* Clear IF and DF on entry so the handler runs with interrupts off. */
    wrmsr(MSR_SFMASK, 0x7);

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