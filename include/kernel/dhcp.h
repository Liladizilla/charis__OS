/* dhcp.h - Bounded DHCPv4 client. */
#pragma once

#include <kernel/types.h>

typedef enum {
    DHCP_STATE_DISABLED = 0,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_RENEWING,
    DHCP_STATE_REBINDING
} dhcp_state_t;

typedef struct {
    u8 message_type;
    u8 address[4];
    u8 server[4];
    u8 subnet_mask[4];
    u8 router[4];
    u8 dns_server[4];
    u32 lease_seconds;
    u32 renewal_seconds;
    u32 rebinding_seconds;
} dhcp_reply_t;

/* Decode and validate a BOOTREPLY against transaction ID and client MAC.
 * Returns only OFFER, ACK or NAK messages. Options are strictly length-bounded. */
bool dhcp_decode_reply(const u8* packet, usize length, u32 transaction_id,
                       const u8 client_mac[6], dhcp_reply_t* reply);

void dhcp_init(void);
void dhcp_poll(void);
dhcp_state_t dhcp_get_state(void);
bool dhcp_is_bound(void);
