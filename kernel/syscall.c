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

    /* Fork needs the interrupted user register frame so the child can resume
     * at the call site. This handler receives only syscall arguments, so fail
     * rather than create a child with an invalid entry frame. Ring-3 startup
     * and static ELF exec are tested; process cloning remains unimplemented. */
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
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return (u64)-1;
}

static bool syscall_copy_user_string(task_t* task, u64 user_address,
                                     char* destination, usize capacity) {
    if (!task || !task->mm.pml4 || !user_address || !destination || !capacity ||
        user_address >= 0x0000800000000000ULL) return false;

    for (usize i = 0; i < capacity; i++) {
        u64 address = user_address + i;
        if (address < user_address || address >= 0x0000800000000000ULL) return false;
        pte_t* pte = vmm_walk(task->mm.pml4, address, false);
        if (!pte || !(*pte & VMM_FLAG_PRESENT) || !(*pte & VMM_FLAG_USER)) return false;
        u64 phys = (*pte & ~0xFFFULL) | (address & 0xFFFULL);
        destination[i] = *(const char*)(uintptr_t)phys;
        if (!destination[i]) return true;
    }
    destination[capacity - 1] = '\0';
    return false;
}

static bool syscall_copy_user_data(task_t* task, u64 user_address,
                                   void* destination, usize size) {
    if (!task || !task->mm.pml4 || !destination ||
        user_address >= 0x0000800000000000ULL ||
        size > 0x0000800000000000ULL - user_address) return false;

    usize copied = 0;
    while (copied < size) {
        u64 address = user_address + copied;
        pte_t* pte = vmm_walk(task->mm.pml4, address, false);
        if (!pte || !(*pte & VMM_FLAG_PRESENT) || !(*pte & VMM_FLAG_USER)) return false;
        u64 phys = (*pte & ~0xFFFULL) | (address & 0xFFFULL);
        usize chunk = PAGE_SIZE - (usize)(address & (PAGE_SIZE - 1));
        if (chunk > size - copied) chunk = size - copied;
        kmemcpy((u8*)destination + copied, (const void*)(uintptr_t)phys, chunk);
        copied += chunk;
    }
    return true;
}

static bool syscall_copy_user_vector(task_t* task, u64 user_vector,
                                     char** strings, u32* string_count,
                                     char* storage, usize storage_size,
                                     usize* storage_used) {
    *string_count = 0;
    if (!user_vector) return true;

    for (u32 i = 0; i < 16; i++) {
        u64 pointer_address = user_vector + (u64)i * sizeof(u64);
        u64 string_address;
        if (pointer_address < user_vector ||
            !syscall_copy_user_data(task, pointer_address, &string_address,
                                    sizeof(string_address))) return false;
        if (!string_address) {
            *string_count = i;
            return true;
        }
        if (*storage_used >= storage_size) return false;

        strings[i] = storage + *storage_used;
        usize remaining = storage_size - *storage_used;
        if (!syscall_copy_user_string(task, string_address, strings[i], remaining)) return false;
        *storage_used += kstrlen(strings[i]) + 1;
    }
    return false;
}

bool syscall_exec_from_frame(reg_frame_t* frame) {
    if (!frame || (frame->cs & 3) != 3) return false;
    task_t* task = scheduler_current();
    if (!task || !task->is_user || !task->mm.pml4 ||
        task->address_space != task->mm.pml4 ||
        !security_check_capability(task, CAP_SPAWN)) return false;

    char path[256];
    if (!syscall_copy_user_string(task, frame->rdi, path, sizeof(path)) ||
        !security_verify_path(path, task)) return false;

    char** strings = (char**)kmalloc(4096);
    char* storage = (char*)kmalloc(4096);
    if (!strings || !storage) {
        if (strings) kfree(strings);
        if (storage) kfree(storage);
        return false;
    }

    char** argv = strings;
    char** envp = strings + 16;
    u32 argc = 0;
    u32 envc = 0;
    usize storage_used = 0;
    bool vectors_valid =
        syscall_copy_user_vector(task, frame->rsi, argv, &argc, storage, 4096,
                                 &storage_used) &&
        syscall_copy_user_vector(task, frame->rdx, envp, &envc, storage, 4096,
                                 &storage_used);
    if (!vectors_valid) {
        kfree(storage);
        kfree(strings);
        return false;
    }

    u64 entry_point;
    if (elf_load(path, &task->mm, &entry_point) < 0) {
        kfree(storage);
        kfree(strings);
        return false;
    }

    u64 stack_top;
    bool stack_ready = elf_setup_stack(&task->mm, &stack_top,
        (const char* const*)argv, (int)argc,
        (const char* const*)envp, (int)envc) == 0;
    kfree(storage);
    kfree(strings);
    if (!stack_ready) return false;

    vmm_switch(task->mm.pml4);
    task->mm.stack_top = stack_top;
    task->user_rsp = stack_top;
    frame->rip = entry_point;
    frame->cs = 0x1B;
    frame->rflags = 0x202;
    frame->rsp = stack_top;
    frame->ss = 0x23;
    frame->rax = 0;
    return true;
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