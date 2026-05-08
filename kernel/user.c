// user.c - Simple user-mode program
#include <kernel/syscall.h>

void user_main(void) {
    sys_print("Hello from user mode!\n");
    sys_yield();
    sys_print("Back in user mode after yield.\n");
    while (1) {
        sys_yield();
    }
}