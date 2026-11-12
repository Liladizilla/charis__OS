/* task_wait.h - waitpid selector and result policy shared with host tests. */
#pragma once
#include <kernel/types.h>

static inline bool task_wait_selector_valid(s64 selector) {
    return selector == -1 || selector > 0;
}
static inline bool task_wait_child_matches(u32 parent_pid, u32 child_parent_pid,
                                           u32 child_pid, s64 selector) {
    if (!parent_pid || child_parent_pid != parent_pid || !child_pid ||
        !task_wait_selector_valid(selector)) return false;
    return selector == -1 || (u64)selector == (u64)child_pid;
}
static inline bool task_wait_nohang_should_return(bool nohang, bool has_child,
                                                   bool has_zombie) {
    return nohang && has_child && !has_zombie;
}
