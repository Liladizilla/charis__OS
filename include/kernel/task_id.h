/* task_id.h - fail-closed allocation for 32-bit task identities */
#pragma once
#include <kernel/types.h>

/*
 * PIDs are embedded in task-bound VFS descriptor handles. Never wrap and
 * recycle an identity: doing so could make a token from an exited task match
 * a later task that happens to reuse a descriptor slot and generation.
 *
 * Returns zero after the 32-bit PID space is exhausted. The counter is left
 * beyond the limit so every subsequent request also fails closed.
 */
static inline u32 task_pid_allocate(u64* next_pid) {
    if (!next_pid) return 0;

    /*
     * PID allocation can be requested by different task-creation paths. Use
     * an atomic compare/exchange so uniqueness does not depend on interrupt
     * masking or a single-CPU assumption. Relaxed ordering is sufficient:
     * this counter establishes uniqueness, not publication of task contents.
     */
    u64 current = __atomic_load_n(next_pid, __ATOMIC_RELAXED);
    for (;;) {
        if (current == 0 || current > 0xFFFFFFFFULL) return 0;

        u64 following = current + 1;
        if (__atomic_compare_exchange_n(next_pid, &current, following, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return (u32)current;
        }
    }
}
