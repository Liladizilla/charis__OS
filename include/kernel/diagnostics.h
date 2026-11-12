/* diagnostics.h - System diagnostics for CharisOS */
#pragma once
#include <kernel/types.h>

typedef struct {
    u64 uptime_ms;
    u32 processes_active;
    u32 processes_total;
    u32 processes_blocked;
    u32 memory_used;
    u32 memory_free;
    u32 context_switches;
    u32 interrupts;
    u32 syscalls;
} system_stats_t;

void diag_init(void);
void diag_collect_stats(system_stats_t* stats);
/* O(1) snapshot for desktop repaint; unlike diag_collect_stats(), this does
 * not traverse the scheduler's task list. */
void diag_get_runtime_counters(u32* context_switches, u32* interrupts,
                              u32* syscalls);
void diag_note_context_switch(void);
void diag_note_interrupt(void);
void diag_note_syscall(void);
void diag_print_status(void);
void diag_dump_tasks(void);