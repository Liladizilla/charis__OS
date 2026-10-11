/* dns.h - Small asynchronous IPv4 DNS A-record resolver. */
#pragma once
#include <kernel/types.h>

typedef void (*net_dns_callback_t)(const char* hostname, const u8 ipv4[4],
                                   bool success, void* context);

/* Initialize the resolver after the Ethernet interface is available. */
bool net_dns_init(void);
/* Queue one hostname lookup. Completion runs in the network polling task. */
bool net_dns_query(const char* hostname, net_dns_callback_t callback,
                   void* context);
/* Advance retries/timeouts from the network polling task. */
void net_dns_poll(void);
