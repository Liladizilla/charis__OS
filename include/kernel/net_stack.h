/* net_stack.h - Polling protocol stack interface. */
#pragma once
#include <kernel/types.h>

void net_stack_init(void);
void net_handle_tcpip(void);
void net_poll_task(void* unused);
