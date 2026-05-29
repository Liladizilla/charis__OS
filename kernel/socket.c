#include <kernel/socket.h>
#include <kernel/net.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/vfs.h>

static socket_t sockets[SOCKET_MAX];

void socket_init(void) {
    for (int i = 0; i < SOCKET_MAX; i++) {
        sockets[i].fd = -1;
        sockets[i].connected = false;
        sockets[i].listening = false;
    }
    vga_puts("Socket subsystem initialized\n");
}

int socket_create(int domain, int type) {
    if (domain != AF_INET) return -1;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -1;
    
    for (int i = 0; i < SOCKET_MAX; i++) {
        if (sockets[i].fd == -1) {
            sockets[i].fd = i;
            sockets[i].type = type;
            sockets[i].connected = false;
            sockets[i].listening = false;
            return i;
        }
    }
    return -1;
}

int socket_connect(int sockfd, u32 ip, u16 port) {
    if (sockfd < 0 || sockfd >= SOCKET_MAX) return -1;
    if (sockets[sockfd].fd == -1) return -1;
    
    sockets[sockfd].ip = ip;
    sockets[sockfd].port = port;
    sockets[sockfd].connected = true;
    
    return 0;
}

int socket_bind(int sockfd, u16 port) {
    if (sockfd < 0 || sockfd >= SOCKET_MAX) return -1;
    if (sockets[sockfd].fd == -1) return -1;
    
    sockets[sockfd].port = port;
    return 0;
}

int socket_listen(int sockfd) {
    if (sockfd < 0 || sockfd >= SOCKET_MAX) return -1;
    if (sockets[sockfd].fd == -1) return -1;
    
    sockets[sockfd].listening = true;
    return 0;
}

int socket_accept(int sockfd) {
    if (sockfd < 0 || sockfd >= SOCKET_MAX) return -1;
    if (sockets[sockfd].fd == -1) return -1;
    if (!sockets[sockfd].listening) return -1;
    
    // For now, just return a new socket (would need connection queue in real implementation)
    return socket_create(AF_INET, SOCK_STREAM);
}

int socket_send(int sockfd, void* buf, usize len) {
    if (sockfd < 0 || sockfd >= SOCKET_MAX) return -1;
    if (sockets[sockfd].fd == -1) return -1;
    if (!sockets[sockfd].connected) return -1;
    
    // Build Ethernet frame and send
    return (int)len; // Simulated success
}

int socket_recv(int sockfd, void* buf, usize max_len) {
    if (sockfd < 0 || sockfd >= SOCKET_MAX) return -1;
    if (sockets[sockfd].fd == -1) return -1;
    
    // Receive packet from network
    return net_recv_packet(buf, max_len);
}

void socket_close(int sockfd) {
    if (sockfd >= 0 && sockfd < SOCKET_MAX) {
        sockets[sockfd].fd = -1;
        sockets[sockfd].connected = false;
        sockets[sockfd].listening = false;
    }
}