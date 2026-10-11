#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/memory.h>
#include <kernel/vga.h>

signal_handler_t signal_handlers[TASK_MAX_TASKS][SIG_MAX];
uint32_t pending_signals[TASK_MAX_TASKS]; /* Bitmask of pending signals */
signal_action_t signal_defaults[SIG_MAX];

/* PIDs are monotonic; only slot_index is safe for these fixed-size tables. */
static int signal_slot(const task_t* task) {
    if (!task || task->slot_index >= TASK_MAX_TASKS) return -1;
    return (int)task->slot_index;
}

void signal_reset_task(task_t* task) {
    int slot = signal_slot(task);
    if (slot < 0) return;
    for (int sig = 0; sig < SIG_MAX; sig++) signal_handlers[slot][sig] = NULL;
    pending_signals[slot] = 0;
}

void signal_init(void) {
    for (int t = 0; t < TASK_MAX_TASKS; t++) {
        for (int s = 0; s < SIG_MAX; s++) {
            signal_handlers[t][s] = NULL;
        }
        pending_signals[t] = 0;
    }

    /* Set default actions */
    signal_defaults[SIG_INTSIG] = SIG_DEFAULT;
    signal_defaults[SIG_TERMINATE] = SIG_DEFAULT;
    signal_defaults[SIG_SEGV] = SIG_DEFAULT;
    signal_defaults[SIG_CHLD] = SIG_IGNORED;
    signal_defaults[SIG_KILL] = SIG_DEFAULT;

    vga_puts("Signal subsystem initialized\n");
}

int signal_register(int sig, signal_handler_t handler) {
    task_t* task = scheduler_current();
    int slot = signal_slot(task);
    if (slot < 0 || sig < 0 || sig >= SIG_MAX || handler == NULL) return -1;

    signal_handlers[slot][sig] = handler;
    return 0;
}

int signal_unregister(int sig) {
    task_t* task = scheduler_current();
    int slot = signal_slot(task);
    if (slot < 0 || sig < 0 || sig >= SIG_MAX) return -1;

    signal_handlers[slot][sig] = NULL;
    return 0;
}

int signal_send(task_t* task, int sig) {
    int slot = signal_slot(task);
    if (slot < 0 || sig < 0 || sig >= SIG_MAX) return -1;

    /* SIGKILL cannot be caught or ignored. */
    if (sig == SIG_KILL) {
        pending_signals[slot] |= (1U << sig);
        return 0;
    }

    if (signal_handlers[slot][sig] == NULL && signal_defaults[sig] == SIG_IGNORED) {
        return 0;
    }

    pending_signals[slot] |= (1U << sig);
    return 0;
}

bool signal_has_pending(int sig) {
    task_t* task = scheduler_current();
    int slot = signal_slot(task);
    if (slot < 0 || sig < 0 || sig >= SIG_MAX) return false;
    return (pending_signals[slot] & (1U << sig)) != 0;
}

void signal_clear_pending(int sig) {
    task_t* task = scheduler_current();
    int slot = signal_slot(task);
    if (slot < 0 || sig < 0 || sig >= SIG_MAX) return;
    pending_signals[slot] &= ~(1U << sig);
}

int signal_check_pending_for_task(task_t* task) {
    int slot = signal_slot(task);
    if (slot < 0) return -1;

    uint32_t pending = pending_signals[slot];
    for (int sig = 0; sig < SIG_MAX; sig++) {
        if (pending & (1U << sig)) return sig;
    }
    return -1;
}

void signal_dispatch(task_t* task) {
    int slot = signal_slot(task);
    if (slot < 0) return;

    int sig = signal_check_pending_for_task(task);
    if (sig < 0) return;

    signal_handler_t handler = signal_handlers[slot][sig];

    if (handler) {
        handler(sig);
        pending_signals[slot] &= ~(1U << sig);
    } else {
        switch (signal_defaults[sig]) {
            case SIG_DEFAULT:
                /* Terminate the process */
                task_exit();
                break;
            case SIG_IGNORED:
            case SIG_HANDLER:
                pending_signals[slot] &= ~(1U << sig);
                break;
        }
    }
}

void signal_set_default(int sig, signal_action_t action) {
    if (sig >= 0 && sig < SIG_MAX) {
        signal_defaults[sig] = action;
    }
}