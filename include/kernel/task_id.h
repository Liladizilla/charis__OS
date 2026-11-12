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
    if (!next_pid || *next_pid == 0 || *next_pid > 0xFFFFFFFFULL) return 0;
    u32 pid = (u32)*next_pid;
    (*next_pid)++;
    return pid;
}
