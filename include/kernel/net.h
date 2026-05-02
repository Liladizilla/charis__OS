/* net.h - Network driver interface */
#pragma once
#include <kernel/types.h>

#define NET_MAX_PACKET_SIZE 1536
#define NET_ETHERNET_ADDR_LEN 6

/* Ethernet frame header */
typedef struct {
    u8 dest[NET_ETHERNET_ADDR_LEN];
    u8 src[NET_ETHERNET_ADDR_LEN];
    u16 ethertype;
} eth_header_t;

/* Network interface */
typedef struct {
    u8 mac_addr[NET_ETHERNET_ADDR_LEN];
    u8 ip_addr[4];
    bool initialized;
    u32 packets_rx;
    u32 packets_tx;
} net_interface_t;

/* Initialize network driver */
void net_init(void);

/* Get network interface */
net_interface_t* net_get_interface(void);

/* Send packet */
int net_send_packet(const void* data, usize len);

/* Receive packet - returns number of bytes received */
int net_recv_packet(void* buffer, usize max_len);

/* Basic TCP/IP support */
void net_handle_tcpip(void);