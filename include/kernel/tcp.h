/* tcp.h - Minimal asynchronous IPv4 TCP client interface. */
#pragma once
#include <kernel/types.h>

typedef void (*net_tcp_connect_callback_t)(bool connected, void* context);
/* Received bytes are borrowed and valid only during the callback. */
typedef void (*net_tcp_data_callback_t)(const u8* data, usize length,
                                        void* context);
typedef void (*net_tcp_close_callback_t)(bool clean, void* context);

/* This early implementation supports one active client connection at a time. */
bool net_tcp_init(void);
bool net_tcp_connect(const u8 destination_ip[4], u16 destination_port,
                     net_tcp_connect_callback_t on_connect,
                     net_tcp_data_callback_t on_data,
                     net_tcp_close_callback_t on_close, void* context);
/* Returns bytes accepted, -1 on invalid/state error, -2 if an earlier send is pending. */
int net_tcp_send(const void* data, usize length);
void net_tcp_close(void);
void net_tcp_input(const u8 source_ip[4], const u8 destination_ip[4],
                   const u8* segment, usize length);
void net_tcp_poll(void);
