// user.c - Simple user-mode program
#include <kernel/syscall.h>
#include <kernel/syscall_wrappers.h>

__attribute__((no_stack_protector))
void user_main(void) {
    sys_print("Hello from user mode!\n");
    char elf_path[] = "/TEST.ELF";
    char program_name[] = "TEST.ELF";
    char argument[] = "ARG_OK";
    char environment[] = "ENV_OK=1";
    char* argv[] = {program_name, argument, NULL};
    char* envp[] = {environment, NULL};
    if (sys_execve(elf_path, argv, envp) == (u64)-1) {
        sys_print("ELF exec unavailable\n");
    }
    sys_yield();
    sys_print("Back in user mode after yield.\n");
    while (1) {
        sys_yield();
    }
}