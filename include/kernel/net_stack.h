/* net_stack.h - Polling protocol stack interface. */
#pragma once
#include <kernel/types.h>

void net_stack_init(void);
void net_handle_tcpip(void);
void net_poll_task(void* unused);


/* UDP callbacks run in the network polling task. Payload and source_ip point
 * into the receive buffer and are valid only for the duration of the callback. */
typedef void (*net_udp_handler_t)(u16 source_port, const u8 source_ip[4],
                                  const u8* payload, usize length);
bool net_udp_bind(u16 port, net_udp_handler_t handler);
void net_udp_unbind(u16 port);
