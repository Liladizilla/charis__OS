/* net_stack.h - Polling protocol stack interface. */
#pragma once
#include <kernel/types.h>

void net_stack_init(void);
/* Resolve a neighbour; on cache miss, sends ARP and returns false. */
bool net_stack_resolve_ipv4(const u8 ip[4], u8 mac_out[6]);
void net_handle_tcpip(void);
void net_poll_task(void* unused);
