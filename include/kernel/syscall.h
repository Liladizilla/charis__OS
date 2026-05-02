/* syscall.h - System call interface */
#pragma once
#include <kernel/types.h>

#define SYSCALL_MAX             32

/* Syscall numbers */
#define SYS_EXIT                0
#define SYS_PRINT               1
#define SYS_OPEN                2
#define SYS_CLOSE               3
#define SYS_READ                4
#define SYS_WRITE               5
#define SYS_SEEK                6
#define SYS_LIST                7
#define SYS_EXEC                8
#define SYS_YIELD               9
#define SYS_SLEEP               10
#define SYS_MALLOC              11
#define SYS_FREE                12
#define SYS_GET_TICKS           13

typedef u64 (*syscall_handler_t)(u64, u64, u64, u64, u64, u64);

void syscall_init(void);
u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);
void syscall_register(u64 num, syscall_handler_t handler);
u64 sys_print(const char* str);

