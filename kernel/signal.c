#include <kernel/signal.h>
#include <kernel/task.h>
#include <kernel/memory.h>
#include <kernel/vga.h>

signal_handler_t signal_handlers[TASK_MAX_TASKS][SIG_MAX];
uint32_t pending_signals[TASK_MAX_TASKS]; /* Bitmask of pending signals */
signal_action_t signal_defaults[SIG_MAX];

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
    if (!task || sig < 0 || sig >= SIG_MAX) return -1;
    if (handler == NULL) return -1;
    
    signal_handlers[task->pid][sig] = handler;
    return 0;
}

int signal_unregister(int sig) {
    task_t* task = scheduler_current();
    if (!task || sig < 0 || sig >= SIG_MAX) return -1;
    
    signal_handlers[task->pid][sig] = NULL;
    return 0;
}

int signal_send(task_t* task, int sig) {
    if (!task || sig < 0 || sig >= SIG_MAX) return -1;
    
    /* SIGKILL cannot be caught or ignored */
    if (sig == SIG_KILL) {
        pending_signals[task->pid] |= (1 << sig);
        return 0;
    }
    
    /* Check if signal is blocked/ignored */
    if (signal_handlers[task->pid][sig] == NULL && signal_defaults[sig] == SIG_IGNORED) {
        return 0;
    }
    
    pending_signals[task->pid] |= (1 << sig);
    return 0;
}

bool signal_has_pending(int sig) {
    task_t* task = scheduler_current();
    if (!task) return false;
    return (pending_signals[task->pid] & (1 << sig)) != 0;
}

void signal_clear_pending(int sig) {
    task_t* task = scheduler_current();
    if (!task) return;
    pending_signals[task->pid] &= ~(1 << sig);
}

int signal_check_pending(void) {
    task_t* task = scheduler_current();
    if (!task) return -1;
    
    uint32_t pending = pending_signals[task->pid];
    for (int s = 0; s < SIG_MAX; s++) {
        if (pending & (1 << s)) {
            return s;
        }
    }
    return -1;
}

void signal_dispatch(task_t* task) {
    int sig = signal_check_pending();
    if (sig < 0) return;
    
    signal_handler_t handler = signal_handlers[task->pid][sig];
    
    if (handler) {
        /* Call custom handler */
        handler(sig);
        signal_clear_pending(sig);
    } else {
        switch (signal_defaults[sig]) {
            case SIG_DEFAULT:
                /* Terminate the process */
                task_exit();
                break;
            case SIG_IGNORED:
                signal_clear_pending(sig);
                break;
            case SIG_HANDLER:
                signal_clear_pending(sig);
                break;
        }
    }
}

void signal_set_default(int sig, signal_action_t action) {
    if (sig >= 0 && sig < SIG_MAX) {
        signal_defaults[sig] = action;
    }
}