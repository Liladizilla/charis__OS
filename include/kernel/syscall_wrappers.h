/* syscall_wrappers.h - C library wrappers for syscalls */
#pragma once

// Process control
#define sys_exit(code) syscall_invoke(SYS_EXIT, (code), 0, 0, 0, 0, 0)
#define sys_yield() syscall_invoke(SYS_YIELD, 0, 0, 0, 0, 0, 0)
#define sys_getpid() syscall_invoke(SYS_GETPID, 0, 0, 0, 0, 0, 0)

// I/O
#define sys_read(fd, buf, count) syscall_invoke(SYS_READ, (fd), (buf), (count), 0, 0, 0)
#define sys_write(fd, buf, count) syscall_invoke(SYS_WRITE, (fd), (buf), (count), 0, 0, 0)
#define sys_print(s) syscall_invoke(SYS_PRINT, (u64)(s), 0, 0, 0, 0, 0)

// File operations
#define sys_open(path) syscall_invoke(SYS_OPEN, (u64)(path), 0, 0, 0, 0, 0)
#define sys_close(fd) syscall_invoke(SYS_CLOSE, (fd), 0, 0, 0, 0, 0)
#define sys_exec(path) syscall_invoke(SYS_EXEC, (u64)(path), 0, 0, 0, 0, 0)

// IPC
#define sys_ipc_create(name) syscall_invoke(SYS_IPC_CREATE, (u64)(name), 0, 0, 0, 0, 0)
#define sys_ipc_send(ch, data, sz) syscall_invoke(SYS_IPC_SEND, (ch), (u64)(data), (sz), 0, 0, 0)
#define sys_ipc_recv(ch, buf, max) syscall_invoke(SYS_IPC_RECV, (ch), (u64)(buf), (max), 0, 0, 0)

// Shared memory
#define sys_shm_alloc() syscall_invoke(SYS_SHM_ALLOC, 0, 0, 0, 0, 0, 0)
#define sys_shm_get(id) (void*)syscall_invoke(SYS_SHM_GET, (id), 0, 0, 0, 0, 0)
#define sys_shm_free(id) syscall_invoke(SYS_SHM_FREE, (id), 0, 0, 0, 0, 0)

// Sockets
#define sys_socket(domain, type) syscall_invoke(SYS_SOCKET, (domain), (type), 0, 0, 0, 0)
#define sys_connect(s, ip, port) syscall_invoke(SYS_CONNECT, (s), (ip), (port), 0, 0, 0)
#define sys_bind(s, port) syscall_invoke(SYS_BIND, (s), (port), 0, 0, 0, 0)
#define sys_listen(s) syscall_invoke(SYS_LISTEN, (s), 0, 0, 0, 0, 0)
#define sys_accept(s) syscall_invoke(SYS_ACCEPT, (s), 0, 0, 0, 0, 0)
#define sys_send(s, buf, len) syscall_invoke(SYS_SEND, (s), (u64)(buf), (len), 0, 0, 0)
#define sys_recv(s, buf, max) syscall_invoke(SYS_RECV, (s), (u64)(buf), (max), 0, 0, 0)
#define sys_socket_close(s) syscall_invoke(SYS_SOCKET_CLOSE, (s), 0, 0, 0, 0, 0)

// External syscall dispatch function
extern u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);

static inline u64 syscall_invoke(u64 number, u64 a1, u64 a2, u64 a3,
                                 u64 a4, u64 a5, u64 a6) {
    register u64 arg4 asm("r10") = a4;
    register u64 arg5 asm("r8") = a5;
    register u64 arg6 asm("r9") = a6;
    asm volatile("int $0x80"
                 : "+a"(number)
                 : "D"(a1), "S"(a2), "d"(a3), "r"(arg4), "r"(arg5), "r"(arg6)
                 : "memory");
    return number;
}