// user.c - Bootstrap ring-3 smoke test.
#include <kernel/syscall.h>
#include <kernel/syscall_wrappers.h>

/* The bootstrap task maps its entry page and user stack, not the kernel's
 * general .rodata section. Keep syscall strings on that mapped stack instead
 * of passing pointers to kernel-linked string literals. Proper ELF-loaded
 * programs receive their own mapped read-only segments. */
__attribute__((no_stack_protector, optimize("O0")))
void user_main(void) {
    char greeting[23];
    greeting[0] = 'H'; greeting[1] = 'e'; greeting[2] = 'l';
    greeting[3] = 'l'; greeting[4] = 'o'; greeting[5] = ' ';
    greeting[6] = 'f'; greeting[7] = 'r'; greeting[8] = 'o';
    greeting[9] = 'm'; greeting[10] = ' '; greeting[11] = 'u';
    greeting[12] = 's'; greeting[13] = 'e'; greeting[14] = 'r';
    greeting[15] = ' '; greeting[16] = 'm'; greeting[17] = 'o';
    greeting[18] = 'd'; greeting[19] = 'e'; greeting[20] = '!';
    greeting[21] = '\n'; greeting[22] = '\0';
    sys_print(greeting);

    char elf_path[10];
    elf_path[0] = '/'; elf_path[1] = 'T'; elf_path[2] = 'E';
    elf_path[3] = 'S'; elf_path[4] = 'T'; elf_path[5] = '.';
    elf_path[6] = 'E'; elf_path[7] = 'L'; elf_path[8] = 'F';
    elf_path[9] = '\0';

    char program_name[9];
    program_name[0] = 'T'; program_name[1] = 'E'; program_name[2] = 'S';
    program_name[3] = 'T'; program_name[4] = '.'; program_name[5] = 'E';
    program_name[6] = 'L'; program_name[7] = 'F'; program_name[8] = '\0';

    char argument[7];
    argument[0] = 'A'; argument[1] = 'R'; argument[2] = 'G';
    argument[3] = '_'; argument[4] = 'O'; argument[5] = 'K';
    argument[6] = '\0';

    char environment[9];
    environment[0] = 'E'; environment[1] = 'N'; environment[2] = 'V';
    environment[3] = '_'; environment[4] = 'O'; environment[5] = 'K';
    environment[6] = '='; environment[7] = '1'; environment[8] = '\0';

    char* argv[] = {program_name, argument, NULL};
    char* envp[] = {environment, NULL};
    if (sys_execve(elf_path, argv, envp) == (u64)-1) {
        char exec_unavailable[22];
        exec_unavailable[0] = 'E'; exec_unavailable[1] = 'L';
        exec_unavailable[2] = 'F'; exec_unavailable[3] = ' ';
        exec_unavailable[4] = 'e'; exec_unavailable[5] = 'x';
        exec_unavailable[6] = 'e'; exec_unavailable[7] = 'c';
        exec_unavailable[8] = ' '; exec_unavailable[9] = 'u';
        exec_unavailable[10] = 'n'; exec_unavailable[11] = 'a';
        exec_unavailable[12] = 'v'; exec_unavailable[13] = 'a';
        exec_unavailable[14] = 'i'; exec_unavailable[15] = 'l';
        exec_unavailable[16] = 'a'; exec_unavailable[17] = 'b';
        exec_unavailable[18] = 'l'; exec_unavailable[19] = 'e';
        exec_unavailable[20] = '\n'; exec_unavailable[21] = '\0';
        sys_print(exec_unavailable);
    }

    sys_yield();

    char resumed[32];
    resumed[0] = 'B'; resumed[1] = 'a'; resumed[2] = 'c';
    resumed[3] = 'k'; resumed[4] = ' '; resumed[5] = 'i';
    resumed[6] = 'n'; resumed[7] = ' '; resumed[8] = 'u';
    resumed[9] = 's'; resumed[10] = 'e'; resumed[11] = 'r';
    resumed[12] = ' '; resumed[13] = 'm'; resumed[14] = 'o';
    resumed[15] = 'd'; resumed[16] = 'e'; resumed[17] = ' ';
    resumed[18] = 'a'; resumed[19] = 'f'; resumed[20] = 't';
    resumed[21] = 'e'; resumed[22] = 'r'; resumed[23] = ' ';
    resumed[24] = 'y'; resumed[25] = 'i'; resumed[26] = 'e';
    resumed[27] = 'l'; resumed[28] = 'd'; resumed[29] = '.';
    resumed[30] = '\n'; resumed[31] = '\0';
    sys_print(resumed);

    while (1) {
        sys_yield();
    }
}
