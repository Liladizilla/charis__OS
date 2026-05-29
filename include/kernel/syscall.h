/* syscall.h - System call interface */
#pragma once
#include <kernel/types.h>

#define SYSCALL_MAX             64

/* Syscall numbers (Linux ABI) */
#define SYS_READ                0
#define SYS_WRITE               1
#define SYS_OPEN                2
#define SYS_CLOSE               3
#define SYS_EXIT                60
#define SYS_YIELD               98
#define SYS_PRINT               99
#define SYS_GETPID              39
#define SYS_SLEEP               95
#define SYS_FORK                57
#define SYS_EXEC                59
#define SYS_SHM_ALLOC           96
#define SYS_SHM_GET             97
#define SYS_SHM_FREE            94
#define SYS_IPC_CREATE          93
#define SYS_IPC_SEND            92
#define SYS_IPC_RECV            91
#define SYS_SOCKET              90
#define SYS_CONNECT             89
#define SYS_BIND                88
#define SYS_LISTEN              87
#define SYS_ACCEPT              86
#define SYS_SEND                85
#define SYS_RECV                84
#define SYS_SOCKET_CLOSE        83
#define SYS_DIAG_STATS          82
#define SYS_DIAG_TASKS           81
#define SYS_BEEP                80

typedef u64 (*syscall_handler_t)(u64, u64, u64, u64, u64, u64);

void syscall_init(void);
u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);
void syscall_register(u64 num, syscall_handler_t handler);