/* signal.h - Signal handling for CharisOS */
#pragma once
#include <kernel/types.h>

#define SIG_MAX 32

#define SIG_INTSIG    2   /* Interrupt (Ctrl+C) */
#define SIG_TERMINATE 15  /* Termination request */
#define SIG_SEGV      11  /* Segmentation fault */
#define SIG_CHLD      17  /* Child process changed state */
#define SIG_KILL      9   /* Forced termination */

typedef enum {
    SIG_DEFAULT,    /* Use default action (usually terminate) */
    SIG_IGNORED,   /* Ignore the signal */
    SIG_HANDLER    /* Custom handler installed */
} signal_action_t;

typedef void (*signal_handler_t)(int sig);

void signal_init(void);

int signal_register(int sig, signal_handler_t handler);
int signal_unregister(int sig);

int signal_send(task_t* task, int sig);
int signal_check_pending(void);
void signal_dispatch(task_t* task);

bool signal_has_pending(int sig);
void signal_clear_pending(int sig);

void signal_set_default(int sig, signal_action_t action);