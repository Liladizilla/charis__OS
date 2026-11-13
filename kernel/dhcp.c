/* dhcp.c - DHCPv4 DORA and lease renewal over the RTL8139 IPv4 stack. */
#include <kernel/dhcp.h>
#include <kernel/net.h>
#include <kernel/net_stack.h>
#include <kernel/dns.h>
#include <kernel/config.h>
#include <kernel/timer.h>
#include <kernel/string.h>
#include <kernel/serial.h>

#define DHCP_SERVER_PORT 67u
#define DHCP_CLIENT_PORT 68u
#define DHCP_FIXED_LEN 236u
#define DHCP_COOKIE_LEN 4u
#define DHCP_OPTIONS_OFFSET 240u
#define DHCP_PACKET_CAP 548u
#define DHCP_MAGIC_0 0x63u
#define DHCP_MAGIC_1 0x82u
#define DHCP_MAGIC_2 0x53u
#define DHCP_MAGIC_3 0x63u
#define DHCP_OP_BOOTREQUEST 1u
#define DHCP_OP_BOOTREPLY 2u
#define DHCP_HTYPE_ETHERNET 1u
#define DHCP_HLEN_ETHERNET 6u
#define DHCP_FLAG_BROADCAST 0x8000u
#define DHCP_OPTION_PAD 0u
#define DHCP_OPTION_END 255u
#define DHCP_OPTION_SUBNET 1u
#define DHCP_OPTION_ROUTER 3u
#define DHCP_OPTION_DNS 6u
#define DHCP_OPTION_REQUESTED_IP 50u
#define DHCP_OPTION_LEASE 51u
#define DHCP_OPTION_MESSAGE_TYPE 53u
#define DHCP_OPTION_SERVER_ID 54u
#define DHCP_OPTION_PARAMETER_LIST 55u
#define DHCP_OPTION_RENEWAL 58u
#define DHCP_OPTION_REBINDING 59u
#define DHCP_OPTION_CLIENT_ID 61u
#define DHCP_DISCOVER 1u
#define DHCP_OFFER 2u
#define DHCP_REQUEST 3u
#define DHCP_ACK 5u
#define DHCP_NAK 6u
#define DHCP_DEFAULT_LEASE 3600u

static dhcp_state_t dhcp_state = DHCP_STATE_DISABLED;
static u8 client_mac[6];
static u8 leased_ip[4];
static u8 server_ip[4];
static u32 transaction_id;
static u64 state_started_ms;
static u64 next_retry_ms;
static u64 lease_started_ms;
static u64 lease_duration_ms;
static u64 renewal_at_ms;
static u64 rebinding_at_ms;
static u32 retries;
static bool initialized;
static u8 dhcp_packet[DHCP_PACKET_CAP];

static u32 read_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
           ((u32)p[2] << 8) | p[3];
}

static void write_be16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static void write_be32(u8* p, u32 value) {
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}

static bool bytes_equal(const u8* a, const u8* b, usize length) {
    for (usize i = 0; i < length; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bool ipv4_zero(const u8 ip[4]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static bool ipv4_unicast(const u8 ip[4]) {
    return ip && ip[0] != 0u && ip[0] != 127u && ip[0] < 224u;
}

static bool valid_mask(const u8 mask[4]) {
    u32 value = ((u32)mask[0] << 24) | ((u32)mask[1] << 16) |
                ((u32)mask[2] << 8) | mask[3];
    /* A zero-length prefix makes every IPv4 destination appear on-link,
     * which breaks next-hop routing. /1 through /32 remain supported. */
    if (!value) return false;
    bool zero_seen = false;
    for (u32 bit = 0x80000000u; bit; bit >>= 1) {
        if (!(value & bit)) zero_seen = true;
        else if (zero_seen) return false;
    }
    return true;
}

static bool ipv4_on_subnet(const u8 address[4], const u8 network_ip[4],
                           const u8 mask[4]) {
    if (!address || !network_ip || !mask) return false;
    for (usize i = 0; i < 4; i++)
        if ((address[i] & mask[i]) != (network_ip[i] & mask[i]))
            return false;
    return true;
}

/* DHCP leases must identify a usable host address, not the subnet or
 * directed-broadcast identifier. /31 and /32 have no such exclusions. */
static bool ipv4_subnet_host(const u8 address[4], const u8 mask[4]) {
    if (!ipv4_unicast(address) || !valid_mask(mask)) return false;
    u32 mask_value = ((u32)mask[0] << 24) | ((u32)mask[1] << 16) |
                     ((u32)mask[2] << 8) | mask[3];
    u32 host_mask = ~mask_value;
    if (host_mask <= 1u) return true;

    u32 value = ((u32)address[0] << 24) | ((u32)address[1] << 16) |
                ((u32)address[2] << 8) | address[3];
    u32 host_bits = value & host_mask;
    return host_bits != 0u && host_bits != host_mask;
}

/* Decode bounded DHCP options. Duplicated singleton options are ignored after
 * the first occurrence; the parser never reads beyond the UDP payload. */
static bool decode_options(const u8* packet, usize length,
                           dhcp_reply_t* reply) {
    bool have_type = false, have_server = false, have_mask = false;
    bool have_router = false, have_dns = false, have_lease = false;
    bool have_t1 = false, have_t2 = false, have_end = false;
    usize offset = DHCP_OPTIONS_OFFSET;

    while (offset < length) {
        u8 code = packet[offset++];
        if (code == DHCP_OPTION_PAD) continue;
        if (code == DHCP_OPTION_END) {
            have_end = true;
            break;
        }
        if (offset >= length) return false;
        u8 option_length = packet[offset++];
        if ((usize)option_length > length - offset) return false;
        const u8* value = packet + offset;

        switch (code) {
        case DHCP_OPTION_MESSAGE_TYPE:
            if (!have_type) {
                if (option_length != 1u) return false;
                reply->message_type = value[0];
                have_type = true;
            }
            break;
        case DHCP_OPTION_SERVER_ID:
            if (!have_server) {
                if (option_length != 4u) return false;
                kmemcpy(reply->server, value, 4);
                have_server = true;
            }
            break;
        case DHCP_OPTION_SUBNET:
            if (!have_mask) {
                if (option_length != 4u) return false;
                kmemcpy(reply->subnet_mask, value, 4);
                have_mask = true;
            }
            break;
        case DHCP_OPTION_ROUTER:
            if (!have_router) {
                if (option_length < 4u || (option_length & 3u)) return false;
                kmemcpy(reply->router, value, 4);
                have_router = true;
            }
            break;
        case DHCP_OPTION_DNS:
            if (!have_dns) {
                if (option_length < 4u || (option_length & 3u)) return false;
                kmemcpy(reply->dns_server, value, 4);
                have_dns = true;
            }
            break;
        case DHCP_OPTION_LEASE:
            if (!have_lease) {
                if (option_length != 4u) return false;
                reply->lease_seconds = read_be32(value);
                have_lease = true;
            }
            break;
        case DHCP_OPTION_RENEWAL:
            if (!have_t1) {
                if (option_length != 4u) return false;
                reply->renewal_seconds = read_be32(value);
                have_t1 = true;
            }
            break;
        case DHCP_OPTION_REBINDING:
            if (!have_t2) {
                if (option_length != 4u) return false;
                reply->rebinding_seconds = read_be32(value);
                have_t2 = true;
            }
            break;
        default:
            break;
        }
        offset += option_length;
    }

    if (!have_end || !have_type || !have_server) return false;
    if (reply->message_type != DHCP_OFFER &&
        reply->message_type != DHCP_ACK &&
        reply->message_type != DHCP_NAK)
        return false;

    if (reply->message_type != DHCP_NAK) {
        if (!have_mask) {
            reply->subnet_mask[0] = 255;
            reply->subnet_mask[1] = 255;
            reply->subnet_mask[2] = 255;
            reply->subnet_mask[3] = 0;
        }
        if (!valid_mask(reply->subnet_mask)) return false;

        if (!have_lease || reply->lease_seconds < 3u)
            reply->lease_seconds = DHCP_DEFAULT_LEASE;
        if (!have_t1 || !reply->renewal_seconds ||
            reply->renewal_seconds >= reply->lease_seconds)
            reply->renewal_seconds = reply->lease_seconds / 2u;
        if (!have_t2 || !reply->rebinding_seconds ||
            reply->rebinding_seconds >= reply->lease_seconds)
            reply->rebinding_seconds = (u32)(((u64)reply->lease_seconds * 7u) / 8u);
        if (reply->renewal_seconds >= reply->rebinding_seconds) {
            reply->renewal_seconds = reply->lease_seconds / 2u;
            reply->rebinding_seconds = (reply->lease_seconds * 7u) / 8u;
        }
        if (!reply->renewal_seconds) reply->renewal_seconds = 1u;
        if (reply->rebinding_seconds <= reply->renewal_seconds)
            reply->rebinding_seconds = reply->renewal_seconds + 1u;
        if (reply->rebinding_seconds >= reply->lease_seconds)
            reply->rebinding_seconds = reply->lease_seconds - 1u;
    }
    return true;
}

bool dhcp_decode_reply(const u8* packet, usize length, u32 xid,
                       const u8 mac[6], dhcp_reply_t* reply) {
    if (!packet || !mac || !reply || length < DHCP_OPTIONS_OFFSET ||
        length > DHCP_PACKET_CAP)
        return false;

    if (packet[0] != DHCP_OP_BOOTREPLY ||
        packet[1] != DHCP_HTYPE_ETHERNET ||
        packet[2] != DHCP_HLEN_ETHERNET ||
        read_be32(packet + 4u) != xid ||
        !bytes_equal(packet + 28u, mac, 6) ||
        packet[236] != DHCP_MAGIC_0 ||
        packet[237] != DHCP_MAGIC_1 ||
        packet[238] != DHCP_MAGIC_2 ||
        packet[239] != DHCP_MAGIC_3)
        return false;

    kmemset(reply, 0, sizeof(*reply));
    kmemcpy(reply->address, packet + 16u, 4);
    if (!decode_options(packet, length, reply)) return false;

    if (!ipv4_unicast(reply->server)) return false;

    if (reply->message_type == DHCP_OFFER &&
        !ipv4_subnet_host(reply->address, reply->subnet_mask))
        return false;
    if (reply->message_type == DHCP_ACK && !ipv4_zero(reply->address) &&
        !ipv4_subnet_host(reply->address, reply->subnet_mask))
        return false;

    /* A supplied default router must be a different, usable host on the
     * offered subnet. A DNS server may be remote, but must still be unicast. */
    if (!ipv4_zero(reply->router)) {
        if (!ipv4_subnet_host(reply->router, reply->subnet_mask))
            return false;
        if (!ipv4_zero(reply->address) &&
            (bytes_equal(reply->router, reply->address, 4) ||
             !ipv4_on_subnet(reply->router, reply->address,
                             reply->subnet_mask)))
            return false;
    }
    if (!ipv4_zero(reply->dns_server) && !ipv4_unicast(reply->dns_server))
        return false;
    return true;
}

static void append_option(usize* offset, u8 code, const void* value, u8 length) {
    if (!offset || *offset + 2u + length >= sizeof(dhcp_packet)) return;
    dhcp_packet[(*offset)++] = code;
    dhcp_packet[(*offset)++] = length;
    if (length && value) {
        kmemcpy(dhcp_packet + *offset, value, length);
        *offset += length;
    }
}

static void append_end(usize* offset) {
    if (offset && *offset < sizeof(dhcp_packet))
        dhcp_packet[(*offset)++] = DHCP_OPTION_END;
}

static usize build_message(u8 message_type, bool include_requested_ip,
                           bool include_server_id, bool use_ciaddr,
                           bool broadcast_response) {
    net_interface_t* iface = net_get_interface();
    if (!iface || !iface->initialized) return 0;
    kmemset(dhcp_packet, 0, sizeof(dhcp_packet));

    dhcp_packet[0] = DHCP_OP_BOOTREQUEST;
    dhcp_packet[1] = DHCP_HTYPE_ETHERNET;
    dhcp_packet[2] = DHCP_HLEN_ETHERNET;
    write_be32(dhcp_packet + 4u, transaction_id);
    u64 elapsed = timer_get_ms() - state_started_ms;
    write_be16(dhcp_packet + 8u, elapsed / 1000u > 0xFFFFu
                                  ? 0xFFFFu : (u16)(elapsed / 1000u));
    write_be16(dhcp_packet + 10u,
               broadcast_response ? DHCP_FLAG_BROADCAST : 0u);
    if (use_ciaddr) kmemcpy(dhcp_packet + 12u, leased_ip, 4);
    kmemcpy(dhcp_packet + 28u, iface->mac_addr, 6);
    dhcp_packet[236] = DHCP_MAGIC_0;
    dhcp_packet[237] = DHCP_MAGIC_1;
    dhcp_packet[238] = DHCP_MAGIC_2;
    dhcp_packet[239] = DHCP_MAGIC_3;

    usize offset = DHCP_OPTIONS_OFFSET;
    append_option(&offset, DHCP_OPTION_MESSAGE_TYPE, &message_type, 1u);

    u8 client_id[7];
    client_id[0] = DHCP_HTYPE_ETHERNET;
    kmemcpy(client_id + 1u, iface->mac_addr, 6);
    append_option(&offset, DHCP_OPTION_CLIENT_ID, client_id, sizeof(client_id));

    if (include_requested_ip)
        append_option(&offset, DHCP_OPTION_REQUESTED_IP, leased_ip, 4u);
    if (include_server_id)
        append_option(&offset, DHCP_OPTION_SERVER_ID, server_ip, 4u);

    static const u8 requested[] = {
        DHCP_OPTION_SUBNET, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
        DHCP_OPTION_LEASE, DHCP_OPTION_RENEWAL, DHCP_OPTION_REBINDING
    };
    append_option(&offset, DHCP_OPTION_PARAMETER_LIST,
                  requested, sizeof(requested));
    append_end(&offset);
    return offset;
}

static bool send_discover(void) {
    const u8 zero_ip[4] = {0, 0, 0, 0};
    kmemcpy(leased_ip, zero_ip, 4);
    usize length = build_message(DHCP_DISCOVER, false, false, false, true);
    return length && net_udp_send_broadcast(DHCP_CLIENT_PORT,
                                             DHCP_SERVER_PORT,
                                             dhcp_packet, length) >= 0;
}

static bool send_initial_request(void) {
    usize length = build_message(DHCP_REQUEST, true, true, false, true);
    return length && net_udp_send_broadcast(DHCP_CLIENT_PORT,
                                             DHCP_SERVER_PORT,
                                             dhcp_packet, length) >= 0;
}

static bool send_renew_request(bool broadcast) {
    usize length = build_message(DHCP_REQUEST, false, false, true, broadcast);
    if (!length) return false;
    if (broadcast)
        return net_udp_send_broadcast(DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                                      dhcp_packet, length) >= 0;
    return net_udp_send(server_ip, DHCP_SERVER_PORT, DHCP_CLIENT_PORT,
                        dhcp_packet, length) >= 0;
}

static void advance_transaction_id(void) {
    transaction_id += 0x9E3779B9u;
    if (!transaction_id) transaction_id = 1u;
}

static u64 retry_delay_ms(u32 attempt) {
    u32 shift = attempt > 5u ? 5u : attempt;
    u64 delay = 2000ULL << shift;
    return delay > 60000ULL ? 60000ULL : delay;
}

static void start_discovery(void) {
    const u8 zero[4] = {0, 0, 0, 0};
    kmemcpy(leased_ip, zero, 4);
    kmemcpy(server_ip, zero, 4);
    retries = 0;
    state_started_ms = timer_get_ms();
    transaction_id += 0x9E3779B9u;
    if (!transaction_id) transaction_id = 1u;
    dhcp_state = DHCP_STATE_SELECTING;
    (void)send_discover();
    next_retry_ms = timer_get_ms() + retry_delay_ms(retries++);
    serial_puts("DHCP: DISCOVER sent");
}

static bool apply_lease(const dhcp_reply_t* reply) {
    u8 address[4];
    if (ipv4_zero(reply->address)) kmemcpy(address, leased_ip, 4);
    else kmemcpy(address, reply->address, 4);
    if (!ipv4_unicast(address)) return false;

    u8 gateway[4];
    kmemcpy(gateway, reply->router, 4);
    if (!net_stack_configure_ipv4(address, reply->subnet_mask, gateway))
        return false;

    if (!ipv4_zero(reply->dns_server))
        (void)net_dns_set_server(reply->dns_server);

    if (!ipv4_zero(reply->server))
        kmemcpy(server_ip, reply->server, 4);

    kmemcpy(leased_ip, address, 4);
    if (!reply->lease_seconds) return false;
    lease_duration_ms = (u64)reply->lease_seconds * 1000ULL;
    renewal_at_ms = (u64)reply->renewal_seconds * 1000ULL;
    rebinding_at_ms = (u64)reply->rebinding_seconds * 1000ULL;
    if (!renewal_at_ms || renewal_at_ms >= lease_duration_ms)
        renewal_at_ms = lease_duration_ms / 2u;
    if (rebinding_at_ms <= renewal_at_ms || rebinding_at_ms >= lease_duration_ms)
        rebinding_at_ms = (lease_duration_ms * 7u) / 8u;
    lease_started_ms = timer_get_ms();
    retries = 0;
    next_retry_ms = lease_started_ms + renewal_at_ms;
    dhcp_state = DHCP_STATE_BOUND;
    serial_printf("DHCP: lease acquired %u.%u.%u.%u\n",
                  address[0], address[1], address[2], address[3]);
    return true;
}

static void handle_dhcp_reply(u16 source_port, const u8 source_ip[4],
                              const u8* payload, usize length) {
    if (source_port != DHCP_SERVER_PORT || !source_ip || !payload ||
        !initialized)
        return;
    dhcp_reply_t reply;
    if (!dhcp_decode_reply(payload, length, transaction_id, client_mac, &reply))
        return;

    if (dhcp_state == DHCP_STATE_SELECTING && reply.message_type == DHCP_OFFER) {
        if (!ipv4_unicast(reply.server) || !ipv4_unicast(reply.address)) return;
        kmemcpy(leased_ip, reply.address, 4);
        kmemcpy(server_ip, reply.server, 4);
        dhcp_state = DHCP_STATE_REQUESTING;
        state_started_ms = timer_get_ms();
        retries = 0;
        (void)send_initial_request();
        next_retry_ms = timer_get_ms() + 4000u;
        serial_puts("DHCP: OFFER accepted; REQUEST sent");
        return;
    }

    if (dhcp_state == DHCP_STATE_REQUESTING) {
        if (!bytes_equal(reply.server, server_ip, 4)) return;
        if (reply.message_type == DHCP_NAK) {
            net_stack_clear_ipv4();
            start_discovery();
            return;
        }
        if (reply.message_type != DHCP_ACK) return;
        if (!ipv4_zero(reply.server) && !bytes_equal(reply.server, server_ip, 4))
            return;
        if (!ipv4_zero(reply.address) && !bytes_equal(reply.address, leased_ip, 4))
            return;
        if (ipv4_zero(reply.address)) kmemcpy(reply.address, leased_ip, 4);
        if (ipv4_zero(reply.server)) kmemcpy(reply.server, server_ip, 4);
        if (!apply_lease(&reply)) {
            net_stack_clear_ipv4();
            start_discovery();
            return;
        }
        return;
    }

    if (dhcp_state == DHCP_STATE_RENEWING ||
        dhcp_state == DHCP_STATE_REBINDING) {
        if (dhcp_state == DHCP_STATE_RENEWING &&
            !bytes_equal(reply.server, server_ip, 4))
            return;
        if (reply.message_type == DHCP_NAK) {
            net_stack_clear_ipv4();
            start_discovery();
            return;
        }
        if (reply.message_type != DHCP_ACK) return;
        if (dhcp_state != DHCP_STATE_REBINDING &&
            !ipv4_zero(reply.server) && !bytes_equal(reply.server, server_ip, 4))
            return;
        if (ipv4_zero(reply.address)) kmemcpy(reply.address, leased_ip, 4);
        if (!ipv4_zero(reply.server) || ipv4_zero(server_ip))
            kmemcpy(server_ip, reply.server, 4);
        if (!apply_lease(&reply)) {
            net_stack_clear_ipv4();
            start_discovery();
        }
    }
}

void dhcp_init(void) {
    initialized = false;
    dhcp_state = DHCP_STATE_DISABLED;
    net_interface_t* iface = net_get_interface();
    if (!iface || !iface->initialized) return;

    bool has_static = net_stack_has_ipv4_config();
    bool enabled = config_get_bool("net_dhcp", !has_static);
    if (!enabled) return;

    kmemcpy(client_mac, iface->mac_addr, 6);
    transaction_id = timer_get_ticks() ^ 0xA5C39E71u;
    for (u32 i = 0; i < 6u; i++)
        transaction_id = (transaction_id << 5) ^ (transaction_id >> 2) ^ client_mac[i];
    if (!transaction_id) transaction_id = 1u;

    if (!net_udp_bind(DHCP_CLIENT_PORT, handle_dhcp_reply)) return;
    initialized = true;
    net_stack_clear_ipv4();
    start_discovery();
}

void dhcp_poll(void) {
    if (!initialized) return;
    u64 now = timer_get_ms();

    if (dhcp_state == DHCP_STATE_SELECTING ||
        dhcp_state == DHCP_STATE_REQUESTING) {
        if (now < next_retry_ms) return;
        if (dhcp_state == DHCP_STATE_SELECTING) {
            (void)send_discover();
            next_retry_ms = now + retry_delay_ms(retries++);
        } else {
            if (retries >= 5u) {
                start_discovery();
                return;
            }
            (void)send_initial_request();
            next_retry_ms = now + retry_delay_ms(retries++);
        }
        return;
    }

    if (dhcp_state == DHCP_STATE_BOUND) {
        if (now - lease_started_ms >= lease_duration_ms) {
            net_stack_clear_ipv4();
            start_discovery();
        } else if (now - lease_started_ms >= renewal_at_ms) {
            dhcp_state = DHCP_STATE_RENEWING;
            advance_transaction_id();
            state_started_ms = now;
            retries = 0;
            bool sent = send_renew_request(false);
            u64 until_t2 = rebinding_at_ms -
                           (now - lease_started_ms);
            u64 delay = until_t2 / 2u;
            if (delay < 60000u) delay = 60000u;
            next_retry_ms = now + (sent ? delay : 1000u);
        }
        return;
    }

    if (dhcp_state == DHCP_STATE_RENEWING) {
        if (now - lease_started_ms >= lease_duration_ms) {
            net_stack_clear_ipv4();
            start_discovery();
            return;
        }
        if (now - lease_started_ms >= rebinding_at_ms) {
            dhcp_state = DHCP_STATE_REBINDING;
            advance_transaction_id();
            state_started_ms = now;
            bool sent = send_renew_request(true);
            u64 remaining = lease_duration_ms - (now - lease_started_ms);
            u64 delay = remaining / 2u;
            if (delay < 60000u) delay = 60000u;
            next_retry_ms = now + (sent ? delay : 1000u);
        } else if (now >= next_retry_ms) {
            bool sent = send_renew_request(false);
            u64 until_t2 = rebinding_at_ms -
                           (now - lease_started_ms);
            u64 delay = until_t2 / 2u;
            if (delay < 60000u) delay = 60000u;
            next_retry_ms = now + (sent ? delay : 1000u);
        }
        return;
    }

    if (dhcp_state == DHCP_STATE_REBINDING) {
        if (now - lease_started_ms >= lease_duration_ms) {
            net_stack_clear_ipv4();
            start_discovery();
            return;
        }
        if (now >= next_retry_ms) {
            bool sent = send_renew_request(true);
            u64 remaining = lease_duration_ms - (now - lease_started_ms);
            u64 delay = remaining / 2u;
            if (delay < 60000u) delay = 60000u;
            next_retry_ms = now + (sent ? delay : 1000u);
        }
    }
}

dhcp_state_t dhcp_get_state(void) {
    return dhcp_state;
}

bool dhcp_is_bound(void) {
    return dhcp_state == DHCP_STATE_BOUND ||
           dhcp_state == DHCP_STATE_RENEWING ||
           dhcp_state == DHCP_STATE_REBINDING;
}
