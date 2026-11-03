#include <kernel/diagnostics.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/timer.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/printf.h>
#include <kernel/string.h>

static u32 diag_context_switches = 0;
static u32 diag_interrupts = 0;
static u32 diag_syscalls = 0;

void diag_init(void) {
    diag_context_switches = 0;
    diag_interrupts = 0;
    diag_syscalls = 0;
    vga_puts("Diagnostics initialized\n");
}

void diag_collect_stats(system_stats_t* stats) {
    if (!stats) return;
    stats->uptime_ms = timer_get_ms();
    
    /* A partially initialized scheduler has no current task yet. Bound the
     * walk so a damaged task ring cannot wedge the diagnostics screen. */
    u32 active = 0, total = 0;
    task_t* cur = scheduler_current();
    task_t* t = cur;
    for (u32 i = 0; t && i < TASK_MAX_TASKS; i++) {
        total++;
        if (t->state == TASK_STATE_RUNNING || t->state == TASK_STATE_READY)
            active++;
        t = t->next;
        if (t == cur) break;
    }
    
    stats->processes_active = active;
    stats->processes_total = total;
    
    // Memory stats from PMM
    stats->memory_used = pmm_used_pages() * 4; // KB
    stats->memory_free = pmm_free_pages() * 4; // KB
    
    stats->context_switches = diag_context_switches;
    stats->interrupts = diag_interrupts;
    stats->syscalls = diag_syscalls;
}

void diag_print_status(void) {
    system_stats_t stats;
    diag_collect_stats(&stats);
    
    kprintf("=== System Status ===\n");
    kprintf("Uptime: %llu seconds\n", stats.uptime_ms / 1000);
    kprintf("Processes: %u active / %u total\n", stats.processes_active, stats.processes_total);
    kprintf("Memory: %u KB used / %u KB free\n", stats.memory_used, stats.memory_free);
    kprintf("Context switches: %u\n", stats.context_switches);
    kprintf("Interrupts: %u\n", stats.interrupts);
    kprintf("Syscalls: %u\n", stats.syscalls);
}

void diag_dump_tasks(void) {
    task_t* cur = scheduler_current();
    task_t* t = cur;
    kprintf("=== Task List ===\n");
    if (!t) {
        kprintf("  <no scheduler task>\n");
        return;
    }

    for (u32 i = 0; t && i < TASK_MAX_TASKS; i++) {
        const char* state_str = "UNKNOWN";
        switch (t->state) {
            case TASK_STATE_READY: state_str = "READY"; break;
            case TASK_STATE_RUNNING: state_str = "RUNNING"; break;
            case TASK_STATE_BLOCKED: state_str = "BLOCKED"; break;
            case TASK_STATE_ZOMBIE: state_str = "ZOMBIE"; break;
            case TASK_STATE_UNUSED: state_str = "UNUSED"; break;
        }
        kprintf("  [%u] %s: %s (quantum=%u)\n",
                t->pid, t->name, state_str, t->remaining_quantum);
        t = t->next;
        if (t == cur) break;
    }
}

// Called from scheduler for context switch counting
void diag_note_context_switch(void) {
    diag_context_switches++;
}

// Called from IRQ handler
void diag_note_interrupt(void) {
    diag_interrupts++;
}

// Called from syscall handler
void diag_note_syscall(void) {
    diag_syscalls++;
}