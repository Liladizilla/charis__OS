// user.c - Bootstrap ring-3 smoke test.
#include <kernel/syscall.h>
#include <kernel/syscall_wrappers.h>

/* The bootstrap task maps its entry page and user stack, not the kernel's
 * general .rodata section. Keep syscall strings on that mapped stack instead
 * of passing pointers to kernel-linked string literals. Proper ELF-loaded
 * programs receive their own mapped read-only segments. */
__attribute__((no_stack_protector, optimize("O0"), aligned(4096)))
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
    /* Exercise the real ring-3 sleep path before the yield/resume check.
     * The text is built on the user stack because kernel .rodata is not mapped. */
    sys_sleep(25);
    char sleep_ok[16];
    sleep_ok[0] = 'R'; sleep_ok[1] = 'I'; sleep_ok[2] = 'N';
    sleep_ok[3] = 'G'; sleep_ok[4] = '3'; sleep_ok[5] = '_';
    sleep_ok[6] = 'S'; sleep_ok[7] = 'L'; sleep_ok[8] = 'E';
    sleep_ok[9] = 'E'; sleep_ok[10] = 'P'; sleep_ok[11] = '_';
    sleep_ok[12] = 'O'; sleep_ok[13] = 'K'; sleep_ok[14] = '\n';
    sleep_ok[15] = '\0';
    sys_print(sleep_ok);

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

    /*
     * Regression probes for untrusted syscall pointers. 0x1000 lies in the
     * boot identity map, whose huge-page ancestors are supervisor-only.
     * 0x0000800000000000 is outside the lower canonical user range. Every
     * copy helper must reject both without dereferencing a bogus PTE in ring 0.
     */
    const u64 bad_low = 0x1000ULL;
    const u64 bad_noncanonical = 0x0000800000000000ULL;
    bool syscall_pointers_ok = true;

    if (sys_print((const char*)bad_low) != (u64)-1 ||
        sys_print((const char*)bad_noncanonical) != (u64)-1) {
        syscall_pointers_ok = false;
    }
    if (sys_open((const char*)bad_low) != (u64)-1) syscall_pointers_ok = false;
    if (syscall_invoke(SYS_WRITE, 1, bad_low, 1, 0, 0, 0) != (u64)-1 ||
        syscall_invoke(SYS_WRITE, 1, bad_noncanonical, 1, 0, 0, 0) != (u64)-1) {
        syscall_pointers_ok = false;
    }
    if (syscall_invoke(SYS_DIAG_STATS, bad_low, 0, 0, 0, 0, 0) != (u64)-1 ||
        syscall_invoke(SYS_DIAG_STATS, bad_noncanonical, 0, 0, 0, 0, 0) != (u64)-1) {
        syscall_pointers_ok = false;
    }

    /* IPC and networking remain unavailable to ring 3 until their global
     * tables have per-process ownership and capability-backed handles. */
    if (sys_ipc_create((const char*)bad_low) != (u64)-1 ||
        sys_ipc_create("user-channel") != (u64)-1 ||
        sys_ipc_send(0, (const void*)bad_low, 1) != (u64)-1) {
        syscall_pointers_ok = false;
    }

    if (syscall_pointers_ok) {
        char pointer_ok[29];
        pointer_ok[0] = 'R'; pointer_ok[1] = 'I'; pointer_ok[2] = 'N';
        pointer_ok[3] = 'G'; pointer_ok[4] = '3'; pointer_ok[5] = '_';
        pointer_ok[6] = 'S'; pointer_ok[7] = 'Y'; pointer_ok[8] = 'S';
        pointer_ok[9] = 'C'; pointer_ok[10] = 'A'; pointer_ok[11] = 'L';
        pointer_ok[12] = 'L'; pointer_ok[13] = '_'; pointer_ok[14] = 'P';
        pointer_ok[15] = 'T'; pointer_ok[16] = 'R'; pointer_ok[17] = '_';
        pointer_ok[18] = 'R'; pointer_ok[19] = 'E'; pointer_ok[20] = 'J';
        pointer_ok[21] = 'E'; pointer_ok[22] = 'C'; pointer_ok[23] = 'T';
        pointer_ok[24] = '_'; pointer_ok[25] = 'O'; pointer_ok[26] = 'K';
        pointer_ok[27] = '\n'; pointer_ok[28] = '\0';
        sys_print(pointer_ok);
    } else {
        char pointer_fail[31];
        pointer_fail[0] = 'R'; pointer_fail[1] = 'I'; pointer_fail[2] = 'N';
        pointer_fail[3] = 'G'; pointer_fail[4] = '3'; pointer_fail[5] = '_';
        pointer_fail[6] = 'S'; pointer_fail[7] = 'Y'; pointer_fail[8] = 'S';
        pointer_fail[9] = 'C'; pointer_fail[10] = 'A'; pointer_fail[11] = 'L';
        pointer_fail[12] = 'L'; pointer_fail[13] = '_'; pointer_fail[14] = 'P';
        pointer_fail[15] = 'T'; pointer_fail[16] = 'R'; pointer_fail[17] = '_';
        pointer_fail[18] = 'R'; pointer_fail[19] = 'E'; pointer_fail[20] = 'J';
        pointer_fail[21] = 'E'; pointer_fail[22] = 'C'; pointer_fail[23] = 'T';
        pointer_fail[24] = '_'; pointer_fail[25] = 'F'; pointer_fail[26] = 'A';
        pointer_fail[27] = 'I'; pointer_fail[28] = 'L';
        pointer_fail[29] = '\n'; pointer_fail[30] = '\0';
        sys_print(pointer_fail);
    }

    /* Deliberately touch an unmapped canonical user address. The kernel
     * must terminate only this task, never manufacture a writable page or
     * halt the desktop. The verifier checks the kernel's isolation report. */
    char fault_armed[19];
    fault_armed[0] = 'R'; fault_armed[1] = 'I'; fault_armed[2] = 'N';
    fault_armed[3] = 'G'; fault_armed[4] = '3'; fault_armed[5] = '_';
    fault_armed[6] = 'F'; fault_armed[7] = 'A'; fault_armed[8] = 'U';
    fault_armed[9] = 'L'; fault_armed[10] = 'T'; fault_armed[11] = '_';
    fault_armed[12] = 'A'; fault_armed[13] = 'R'; fault_armed[14] = 'M';
    fault_armed[15] = 'E'; fault_armed[16] = 'D'; fault_armed[17] = '\n';
    fault_armed[18] = '\0';
    sys_print(fault_armed);

    volatile u64 fault_probe =
        *(volatile u64*)0x0000400000000000ULL;
    (void)fault_probe;

    /* Reaching this point means the supposedly unmapped page was accepted. */
    while (1) {
        sys_yield();
    }
}
