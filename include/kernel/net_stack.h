/* net_stack.h - Polling protocol stack interface. */
#pragma once
#include <kernel/types.h>

void net_stack_init(void);
bool net_stack_has_ipv4_config(void);
/* Configure a usable interface host and either zero gateway or a distinct
 * usable host on the same subnet. Contiguous /31 and /32 masks are supported. */
bool net_stack_configure_ipv4(const u8 address[4], const u8 subnet_mask[4],
                              const u8 gateway[4]);
void net_stack_clear_ipv4(void);
/* Transmit an IPv4 packet. Returns -2 when ARP resolution must complete first. */
int net_ipv4_send(const u8 destination_ip[4], u8 protocol,
                  const void* payload, usize length);
/* Resolve only a usable on-link IPv4 neighbour; cache misses transmit ARP
 * and return false. Off-subnet destinations must be routed via the gateway. */
bool net_stack_resolve_ipv4(const u8 ip[4], u8 mac_out[6]);
/* Send UDP over IPv4. Returns bytes sent, -1 on invalid/unavailable route,
 * or -2 when ARP resolution has been started and the caller should retry. */
int net_udp_send(const u8 destination_ip[4], u16 destination_port,
                 u16 source_port, const void* payload, usize length);
int net_udp_send_broadcast(u16 source_port, u16 destination_port,
                           const void* payload, usize length);
void net_handle_tcpip(void);
void net_poll_task(void* unused);


/* UDP callbacks run in the network polling task. Payload and source_ip point
 * into the receive buffer and are valid only for the duration of the callback. */
typedef void (*net_udp_handler_t)(u16 source_port, const u8 source_ip[4],
                                  const u8* payload, usize length);
bool net_udp_bind(u16 port, net_udp_handler_t handler);
void net_udp_unbind(u16 port);
