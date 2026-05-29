/* socket.h - Socket API for CharisOS */
#pragma once
#include <kernel/types.h>

#define SOCK_STREAM   1
#define SOCK_DGRAM    2

#define AF_INET       2

#define SOCKET_MAX    16

typedef struct {
    int fd;
    int type;
    u32 ip;
    u16 port;
    bool connected;
    bool listening;
} socket_t;

// Socket syscalls
int socket_create(int domain, int type);
int socket_connect(int sockfd, u32 ip, u16 port);
int socket_bind(int sockfd, u16 port);
int socket_listen(int sockfd);
int socket_accept(int sockfd);
int socket_send(int sockfd, void* buf, usize len);
int socket_recv(int sockfd, void* buf, usize max_len);
void socket_close(int sockfd);

// Network initialization
void socket_init(void);