/* net_stack.h - Polling protocol stack interface. */
#pragma once
#include <kernel/types.h>

void net_stack_init(void);
/* Transmit an IPv4 packet. Returns -2 when ARP resolution must complete first. */
int net_ipv4_send(const u8 destination_ip[4], u8 protocol,
                  const void* payload, usize length);
/* Resolve an IPv4 next hop; cache misses transmit ARP and return false. */
bool net_stack_resolve_ipv4(const u8 ip[4], u8 mac_out[6]);
/* Send UDP over IPv4. Returns bytes sent, -1 on invalid/unavailable route,
 * or -2 when ARP resolution has been started and the caller should retry. */
int net_udp_send(const u8 destination_ip[4], u16 destination_port,
                 u16 source_port, const void* payload, usize length);
void net_handle_tcpip(void);
void net_poll_task(void* unused);


/* UDP callbacks run in the network polling task. Payload and source_ip point
 * into the receive buffer and are valid only for the duration of the callback. */
typedef void (*net_udp_handler_t)(u16 source_port, const u8 source_ip[4],
                                  const u8* payload, usize length);
bool net_udp_bind(u16 port, net_udp_handler_t handler);
void net_udp_unbind(u16 port);
