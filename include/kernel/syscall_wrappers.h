/* syscall_wrappers.h - C library wrappers for syscalls */
#pragma once

// Process control
#define sys_exit(code) syscall_dispatch(SYS_EXIT, (code), 0, 0, 0, 0, 0)
#define sys_yield() syscall_dispatch(SYS_YIELD, 0, 0, 0, 0, 0, 0)
#define sys_getpid() syscall_dispatch(SYS_GETPID, 0, 0, 0, 0, 0, 0)

// I/O
#define sys_read(fd, buf, count) syscall_dispatch(SYS_READ, (fd), (buf), (count), 0, 0, 0)
#define sys_write(fd, buf, count) syscall_dispatch(SYS_WRITE, (fd), (buf), (count), 0, 0, 0)
#define sys_print(s) syscall_dispatch(SYS_PRINT, (u64)(s), 0, 0, 0, 0, 0)

// File operations
#define sys_open(path) syscall_dispatch(SYS_OPEN, (u64)(path), 0, 0, 0, 0, 0)
#define sys_close(fd) syscall_dispatch(SYS_CLOSE, (fd), 0, 0, 0, 0, 0)
#define sys_exec(path) syscall_dispatch(SYS_EXEC, (u64)(path), 0, 0, 0, 0, 0)

// IPC
#define sys_ipc_create(name) syscall_dispatch(SYS_IPC_CREATE, (u64)(name), 0, 0, 0, 0, 0)
#define sys_ipc_send(ch, data, sz) syscall_dispatch(SYS_IPC_SEND, (ch), (u64)(data), (sz), 0, 0, 0)
#define sys_ipc_recv(ch, buf, max) syscall_dispatch(SYS_IPC_RECV, (ch), (u64)(buf), (max), 0, 0, 0)

// Shared memory
#define sys_shm_alloc() syscall_dispatch(SYS_SHM_ALLOC, 0, 0, 0, 0, 0, 0)
#define sys_shm_get(id) (void*)syscall_dispatch(SYS_SHM_GET, (id), 0, 0, 0, 0, 0)
#define sys_shm_free(id) syscall_dispatch(SYS_SHM_FREE, (id), 0, 0, 0, 0, 0)

// Sockets
#define sys_socket(domain, type) syscall_dispatch(SYS_SOCKET, (domain), (type), 0, 0, 0, 0)
#define sys_connect(s, ip, port) syscall_dispatch(SYS_CONNECT, (s), (ip), (port), 0, 0, 0)
#define sys_bind(s, port) syscall_dispatch(SYS_BIND, (s), (port), 0, 0, 0, 0)
#define sys_listen(s) syscall_dispatch(SYS_LISTEN, (s), 0, 0, 0, 0, 0)
#define sys_accept(s) syscall_dispatch(SYS_ACCEPT, (s), 0, 0, 0, 0, 0)
#define sys_send(s, buf, len) syscall_dispatch(SYS_SEND, (s), (u64)(buf), (len), 0, 0, 0)
#define sys_recv(s, buf, max) syscall_dispatch(SYS_RECV, (s), (u64)(buf), (max), 0, 0, 0)
#define sys_socket_close(s) syscall_dispatch(SYS_SOCKET_CLOSE, (s), 0, 0, 0, 0, 0)

// External syscall dispatch function
extern u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);