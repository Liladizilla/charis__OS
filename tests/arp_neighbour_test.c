/* Host-side ARP neighbour validation and retry regressions. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/types.h>
#include <kernel/net.h>
#include <kernel/net_stack.h>
#include <kernel/config.h>
#include <kernel/timer.h>
#include <kernel/tcp.h>
#include <kernel/dns.h>
#include <kernel/dhcp.h>
#include <kernel/scheduler.h>

static net_interface_t test_iface;
static u64 test_now_ms;
static u8 transmitted[16][NET_MAX_PACKET_SIZE];
static usize transmitted_lengths[16];
static usize transmitted_count;
static u8 received[NET_MAX_PACKET_SIZE];
static usize received_length;

net_interface_t* net_get_interface(void) { return &test_iface; }
int net_send_packet(const void* data, usize length) {
    assert(data != NULL);
    assert(length <= NET_MAX_PACKET_SIZE);
    if (transmitted_count < 16u) {
        memcpy(transmitted[transmitted_count], data, length);
        transmitted_lengths[transmitted_count] = length;
        transmitted_count++;
    }
    return (int)length;
}
int net_recv_packet(void* buffer, usize capacity) {
    if (!received_length) return -1;
    assert(received_length <= capacity);
    memcpy(buffer, received, received_length);
    int result = (int)received_length;
    received_length = 0;
    return result;
}
u64 timer_get_ms(void) { return test_now_ms; }
void* kmemset(void* dst, u8 value, usize length) {
    return memset(dst, value, length);
}
void* kmemcpy(void* dst, const void* src, usize length) {
    return memcpy(dst, src, length);
}
void* kmemmove(void* dst, const void* src, usize length) {
    return memmove(dst, src, length);
}
usize kstrlen(const char* s) { return strlen(s); }
int kstrcmp(const char* a, const char* b) { return strcmp(a, b); }
int kstrncmp(const char* a, const char* b, usize n) { return strncmp(a, b, n); }
char* kstrncpy(char* dst, const char* src, usize n) {
    return strncpy(dst, src, n);
}
static const char* configured_ip_override;
static const char* configured_mask_override;
static const char* configured_gateway_override;

bool config_get_bool(const char* key, bool fallback) {
    (void)key; return fallback;
}
const char* config_get_string(const char* key, const char* fallback) {
    if (strcmp(key, "net_ip") == 0 && configured_ip_override)
        return configured_ip_override;
    if (strcmp(key, "net_mask") == 0 && configured_mask_override)
        return configured_mask_override;
    if (strcmp(key, "net_gateway") == 0 && configured_gateway_override)
        return configured_gateway_override;
    return fallback;
}
void net_tcp_input(const u8 source[4], const u8 destination[4],
                   const u8* segment, usize length) {
    (void)source; (void)destination; (void)segment; (void)length;
}
bool net_dns_init(void) { return true; }
void net_dns_poll(void) {}
bool net_tcp_init(void) { return true; }
void net_tcp_poll(void) {}
void dhcp_init(void) {}
void dhcp_poll(void) {}
void scheduler_yield(void) {}

#include "../kernel/net_stack.c"

static const u8 local_ip_test[4] = {192, 168, 1, 20};
static const u8 local_mac_test[6] = {0x02, 0x10, 0x20, 0x30, 0x40, 0x50};
static const u8 peer_ip_test[4] = {192, 168, 1, 77};
static const u8 peer_mac_test[6] = {0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
static const u8 wrong_mac_test[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const u8 mask_test[4] = {255, 255, 255, 0};
static const u8 zero_mac[6] = {0, 0, 0, 0, 0, 0};
static const u8 broadcast_mac[6] = {255, 255, 255, 255, 255, 255};

static void reset_stack(void) {
    configured_ip_override = NULL;
    configured_mask_override = NULL;
    configured_gateway_override = NULL;
    memset(&test_iface, 0, sizeof(test_iface));
    memcpy(test_iface.mac_addr, local_mac_test, 6);
    test_iface.initialized = true;
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(arp_pending, 0, sizeof(arp_pending));
    memset(udp_bindings, 0, sizeof(udp_bindings));
    arp_cache_next = 0;
    arp_pending_next = 0;
    stack_ready = false;
    transmitted_count = 0;
    received_length = 0;
    test_now_ms = 1000u;
    assert(net_stack_configure_ipv4(local_ip_test, mask_test,
                                    (const u8[4]){192, 168, 1, 1}));
}

static void queue_arp(u16 operation, const u8* ethernet_source,
                      const u8* ethernet_destination, const u8* sender_mac,
                      const u8* sender_ip, const u8* target_mac,
                      const u8* target_ip) {
    memset(received, 0, sizeof(received));
    ethernet_header_t* eth = (ethernet_header_t*)received;
    memcpy(eth->src, ethernet_source, 6);
    memcpy(eth->dest, ethernet_destination, 6);
    write_be16(&eth->type_hi, ETH_TYPE_ARP);

    arp_packet_t* packet = (arp_packet_t*)(received + ETH_HEADER_LEN);
    packet->htype_hi = 0;
    packet->htype_lo = ARP_HTYPE_ETH;
    packet->ptype_hi = 0x08;
    packet->ptype_lo = 0x00;
    packet->hlen = 6;
    packet->plen = 4;
    packet->oper_hi = (u8)(operation >> 8);
    packet->oper_lo = (u8)operation;
    memcpy(packet->sha, sender_mac, 6);
    memcpy(packet->spa, sender_ip, 4);
    memcpy(packet->tha, target_mac, 6);
    memcpy(packet->tpa, target_ip, 4);
    received_length = ETH_HEADER_LEN + sizeof(*packet);
}

static const arp_packet_t* last_sent_arp(void) {
    assert(transmitted_count > 0);
    assert(read_be16(&((const ethernet_header_t*)transmitted[transmitted_count - 1u])->type_hi)
           == ETH_TYPE_ARP);
    return (const arp_packet_t*)(transmitted[transmitted_count - 1u] +
                                 ETH_HEADER_LEN);
}

static void test_cache_miss_requests_are_coalesced_and_retried(void) {
    reset_stack();
    u8 mac[6] = {0};
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);
    const arp_packet_t* request = last_sent_arp();
    assert(read_be16((const u8*)&request->oper_hi) == ARP_REQUEST);
    assert(memcmp(request->spa, local_ip_test, 4) == 0);
    assert(memcmp(request->tpa, peer_ip_test, 4) == 0);
    assert(memcmp(request->sha, local_mac_test, 6) == 0);

    /* Repeated lookups within the retry interval must not flood ARP. */
    for (u32 i = 0; i < 20u; i++)
        assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);

    test_now_ms += ARP_RETRY_INTERVAL_MS - 1u;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);
    test_now_ms++;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 2u);

    for (u32 attempt = 2; attempt < ARP_MAX_ATTEMPTS; attempt++) {
        test_now_ms += ARP_RETRY_INTERVAL_MS;
        assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    }
    assert(transmitted_count == ARP_MAX_ATTEMPTS);

    /* Exhaustion enters a five-second hold-down, then permits a fresh
     * bounded retry cycle rather than flooding on every cache miss. */
    test_now_ms += ARP_RETRY_INTERVAL_MS;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == ARP_MAX_ATTEMPTS);
    test_now_ms += ARP_FAILURE_HOLDDOWN_MS - ARP_RETRY_INTERVAL_MS - 1u;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == ARP_MAX_ATTEMPTS);
    test_now_ms++;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == ARP_MAX_ATTEMPTS + 1u);

    /* A new cycle still follows the one-second cadence and is bounded. */
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == ARP_MAX_ATTEMPTS + 1u);
    for (u32 retry = 1; retry < ARP_MAX_ATTEMPTS; retry++) {
        test_now_ms += ARP_RETRY_INTERVAL_MS;
        assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    }
    assert(transmitted_count == 2u * ARP_MAX_ATTEMPTS);
    test_now_ms += ARP_RETRY_INTERVAL_MS;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 2u * ARP_MAX_ATTEMPTS);
    test_now_ms += ARP_FAILURE_HOLDDOWN_MS - ARP_RETRY_INTERVAL_MS;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 2u * ARP_MAX_ATTEMPTS + 1u);
    test_now_ms += ARP_RETRY_INTERVAL_MS;
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 2u * ARP_MAX_ATTEMPTS + 2u);
}

static void test_only_solicited_matching_arp_reply_is_learned(void) {
    reset_stack();
    u8 mac[6] = {0};
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);

    queue_arp(ARP_REPLY, peer_mac_test, local_mac_test, peer_mac_test,
              peer_ip_test, local_mac_test, local_ip_test);
    net_handle_tcpip();
    assert(received_length == 0);
    assert(arp_pending_find(peer_ip_test) == NULL);
    assert(net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(memcmp(mac, peer_mac_test, 6) == 0);
    assert(transmitted_count == 1u);
}

static void test_unsolicited_and_spoofed_replies_are_ignored(void) {
    reset_stack();
    queue_arp(ARP_REPLY, peer_mac_test, local_mac_test, peer_mac_test,
              peer_ip_test, local_mac_test, local_ip_test);
    net_handle_tcpip();
    u8 mac[6] = {0};
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);

    reset_stack();
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);
    queue_arp(ARP_REPLY, peer_mac_test, local_mac_test, wrong_mac_test,
              peer_ip_test, local_mac_test, local_ip_test);
    net_handle_tcpip();
    assert(arp_pending_find(peer_ip_test) != NULL);
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));

    queue_arp(ARP_REPLY, peer_mac_test, local_mac_test, peer_mac_test,
              peer_ip_test, wrong_mac_test, local_ip_test);
    net_handle_tcpip();
    assert(arp_pending_find(peer_ip_test) != NULL);
    assert(!net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(transmitted_count == 1u);
}

static void test_invalid_sender_identity_and_unrelated_requests_are_ignored(void) {
    reset_stack();
    const u8 other_ip[4] = {192, 168, 1, 88};
    queue_arp(ARP_REQUEST, wrong_mac_test, broadcast_mac, peer_mac_test,
              peer_ip_test, zero_mac, local_ip_test);
    net_handle_tcpip();
    assert(transmitted_count == 0u);
    assert(!arp_pending_find(peer_ip_test));

    queue_arp(ARP_REQUEST, peer_mac_test, broadcast_mac, peer_mac_test,
              other_ip, zero_mac, (const u8[4]){192, 168, 1, 99});
    net_handle_tcpip();
    assert(transmitted_count == 0u);
    assert(!arp_cache[0].valid);

    queue_arp(ARP_REQUEST, peer_mac_test, broadcast_mac, peer_mac_test,
              (const u8[4]){224, 0, 0, 1}, zero_mac, local_ip_test);
    net_handle_tcpip();
    assert(transmitted_count == 0u);
    assert(!arp_cache[0].valid);
}

static void test_valid_request_is_replied_to_and_learned(void) {
    reset_stack();
    queue_arp(ARP_REQUEST, peer_mac_test, broadcast_mac, peer_mac_test,
              peer_ip_test, zero_mac, local_ip_test);
    net_handle_tcpip();
    assert(transmitted_count == 1u);
    const ethernet_header_t* reply_eth =
        (const ethernet_header_t*)transmitted[0];
    assert(memcmp(reply_eth->dest, peer_mac_test, 6) == 0);
    const arp_packet_t* reply = last_sent_arp();
    assert(read_be16((const u8*)&reply->oper_hi) == ARP_REPLY);
    assert(memcmp(reply->spa, local_ip_test, 4) == 0);
    assert(memcmp(reply->tpa, peer_ip_test, 4) == 0);

    u8 mac[6] = {0};
    assert(net_stack_resolve_ipv4(peer_ip_test, mac));
    assert(memcmp(mac, peer_mac_test, 6) == 0);
    assert(transmitted_count == 1u);
}

static void test_ipv4_interface_and_gateway_boundaries(void) {
    reset_stack();
    const u8 network_ip[4] = {192, 168, 1, 0};
    const u8 directed_broadcast[4] = {192, 168, 1, 255};
    const u8 zero_ip[4] = {0, 1, 2, 3};
    const u8 loopback_ip[4] = {127, 0, 0, 1};
    const u8 multicast_ip[4] = {224, 0, 0, 1};
    const u8 class_e_ip[4] = {240, 0, 0, 1};
    const u8 off_subnet_gateway[4] = {10, 0, 0, 1};
    const u8 self_gateway[4] = {192, 168, 1, 20};
    const u8 bad_network_gateway[4] = {192, 168, 1, 0};
    const u8 bad_broadcast_gateway[4] = {192, 168, 1, 255};
    const u8 no_gateway[4] = {0, 0, 0, 0};
    const u8 zero_mask[4] = {0, 0, 0, 0};

    assert(!net_stack_configure_ipv4(local_ip_test, zero_mask, no_gateway));
    assert(!net_stack_configure_ipv4(network_ip, mask_test, no_gateway));
    assert(!net_stack_configure_ipv4(directed_broadcast, mask_test, no_gateway));
    assert(!net_stack_configure_ipv4(zero_ip, mask_test, no_gateway));
    assert(!net_stack_configure_ipv4(loopback_ip, mask_test, no_gateway));
    assert(!net_stack_configure_ipv4(multicast_ip, mask_test, no_gateway));
    assert(!net_stack_configure_ipv4(class_e_ip, mask_test, no_gateway));

    assert(!net_stack_configure_ipv4(local_ip_test, mask_test,
                                     bad_network_gateway));
    assert(!net_stack_configure_ipv4(local_ip_test, mask_test,
                                     bad_broadcast_gateway));
    assert(!net_stack_configure_ipv4(local_ip_test, mask_test,
                                     self_gateway));
    assert(!net_stack_configure_ipv4(local_ip_test, mask_test,
                                     off_subnet_gateway));

    assert(net_stack_configure_ipv4(local_ip_test, mask_test, no_gateway));
    assert(net_stack_has_ipv4_config());
    assert(memcmp(local_ip, local_ip_test, 4) == 0);
    assert(ip_is_zero(default_gateway));
    assert(net_stack_configure_ipv4(local_ip_test, mask_test,
                                    (const u8[4]){192, 168, 1, 1}));
}

static void test_point_to_point_and_host_routes(void) {
    reset_stack();
    const u8 point_to_point_ip[4] = {10, 20, 30, 0};
    const u8 point_to_point_peer[4] = {10, 20, 30, 1};
    const u8 point_to_point_mask[4] = {255, 255, 255, 254};
    const u8 gateway_mac[6] = {0x02, 0xAB, 0xCD, 0xEF, 0x10, 0x20};
    u8 resolved[6] = {0};

    /* RFC 3021 /31 has two usable endpoints and no network/broadcast IDs. */
    assert(net_stack_configure_ipv4(point_to_point_ip, point_to_point_mask,
                                    point_to_point_peer));
    assert(!net_stack_resolve_ipv4(point_to_point_peer, resolved));
    assert(transmitted_count == 1u);
    assert(memcmp(last_sent_arp()->tpa, point_to_point_peer, 4) == 0);
    queue_arp(ARP_REPLY, gateway_mac, local_mac_test, gateway_mac,
              point_to_point_peer, local_mac_test, point_to_point_ip);
    net_handle_tcpip();
    assert(net_stack_resolve_ipv4(point_to_point_peer, resolved));
    assert(memcmp(resolved, gateway_mac, 6) == 0);

    /* A /32 is a valid host route but does not create an on-link peer. */
    reset_stack();
    const u8 host_route_ip[4] = {203, 0, 113, 10};
    const u8 host_route_mask[4] = {255, 255, 255, 255};
    assert(net_stack_configure_ipv4(host_route_ip, host_route_mask,
                                    (const u8[4]){0, 0, 0, 0}));
    assert(net_stack_resolve_ipv4(host_route_ip, resolved));
    assert(memcmp(resolved, local_mac_test, 6) == 0);
    assert(transmitted_count == 0u);
    assert(!net_stack_resolve_ipv4((const u8[4]){203, 0, 113, 11}, resolved));
    assert(transmitted_count == 0u);
}

static void test_off_subnet_neighbours_are_never_arped_directly(void) {
    reset_stack();
    u8 resolved[6] = {0};
    const u8 external_ip[4] = {8, 8, 8, 8};
    assert(!net_stack_resolve_ipv4(external_ip, resolved));
    assert(transmitted_count == 0u);
}

static void test_ipv4_send_uses_gateway_mac_but_keeps_remote_ip(void) {
    reset_stack();
    const u8 gateway_ip[4] = {192, 168, 1, 1};
    const u8 gateway_mac[6] = {0x02, 0x60, 0x70, 0x80, 0x90, 0xA0};
    const u8 external_ip[4] = {8, 8, 8, 8};
    const u8 payload[] = {0xCA, 0xFE, 0x01};
    arp_cache_learn(gateway_ip, gateway_mac);

    assert(net_ipv4_send(external_ip, 99u, payload, sizeof(payload)) > 0);
    assert(transmitted_count == 1u);
    const ethernet_header_t* eth =
        (const ethernet_header_t*)transmitted[0];
    assert(read_be16((const u8*)&eth->type_hi) == ETH_TYPE_IPV4);
    assert(memcmp(eth->dest, gateway_mac, 6) == 0);
    const u8* ip = transmitted[0] + ETH_HEADER_LEN;
    assert(memcmp(ip + 12u, local_ip_test, 4) == 0);
    assert(memcmp(ip + 16u, external_ip, 4) == 0);
    assert(memcmp(ip + IPV4_MIN_LEN, payload, sizeof(payload)) == 0);

    reset_stack();
    arp_cache_learn(gateway_ip, gateway_mac);
    assert(net_udp_send(external_ip, 53u, 40000u,
                        payload, sizeof(payload)) > 0);
    assert(transmitted_count == 1u);
    eth = (const ethernet_header_t*)transmitted[0];
    assert(read_be16((const u8*)&eth->type_hi) == ETH_TYPE_IPV4);
    assert(memcmp(eth->dest, gateway_mac, 6) == 0);
    ip = transmitted[0] + ETH_HEADER_LEN;
    assert(memcmp(ip + 16u, external_ip, 4) == 0);
    assert(read_be16(ip + IPV4_MIN_LEN + 2u) == 53u);
}

static void test_missing_gateway_and_invalid_destinations_fail_closed(void) {
    reset_stack();
    const u8 no_gateway[4] = {0, 0, 0, 0};
    const u8 external_ip[4] = {8, 8, 8, 8};
    const u8 network_ip[4] = {192, 168, 1, 0};
    const u8 broadcast_ip[4] = {192, 168, 1, 255};
    const u8 multicast_ip[4] = {239, 1, 2, 3};
    const u8 payload[] = {1, 2, 3};

    assert(net_stack_configure_ipv4(local_ip_test, mask_test, no_gateway));
    assert(net_ipv4_send(external_ip, 1u, payload, sizeof(payload)) == -1);
    assert(net_udp_send(external_ip, 53u, 40000u,
                        payload, sizeof(payload)) == -1);
    assert(net_ipv4_send(network_ip, 1u, payload, sizeof(payload)) == -1);
    assert(net_ipv4_send(broadcast_ip, 1u, payload, sizeof(payload)) == -1);
    assert(net_ipv4_send(multicast_ip, 1u, payload, sizeof(payload)) == -1);
    assert(transmitted_count == 0u);
}

static void test_persisted_ipv4_configuration_is_validated(void) {
    memset(&test_iface, 0, sizeof(test_iface));
    memcpy(test_iface.mac_addr, local_mac_test, 6);
    test_iface.initialized = true;
    configured_ip_override = "192.168.1.0";
    configured_mask_override = "255.255.255.0";
    configured_gateway_override = "192.168.1.1";
    stack_ready = false;
    net_stack_init();
    assert(!net_stack_has_ipv4_config());
    assert(ip_is_zero(default_gateway));

    configured_ip_override = "192.168.1.20";
    configured_gateway_override = "192.168.1.255";
    stack_ready = false;
    net_stack_init();
    assert(net_stack_has_ipv4_config());
    assert(memcmp(local_ip, local_ip_test, 4) == 0);
    assert(ip_is_zero(default_gateway));

    configured_gateway_override = "192.168.1.1";
    stack_ready = false;
    net_stack_init();
    assert(memcmp(default_gateway, (const u8[4]){192, 168, 1, 1}, 4) == 0);

    configured_ip_override = NULL;
    configured_mask_override = NULL;
    configured_gateway_override = NULL;
}

int main(void) {
    test_cache_miss_requests_are_coalesced_and_retried();
    test_only_solicited_matching_arp_reply_is_learned();
    test_unsolicited_and_spoofed_replies_are_ignored();
    test_invalid_sender_identity_and_unrelated_requests_are_ignored();
    test_valid_request_is_replied_to_and_learned();
    test_ipv4_interface_and_gateway_boundaries();
    test_point_to_point_and_host_routes();
    test_off_subnet_neighbours_are_never_arped_directly();
    test_ipv4_send_uses_gateway_mac_but_keeps_remote_ip();
    test_missing_gateway_and_invalid_destinations_fail_closed();
    test_persisted_ipv4_configuration_is_validated();
    puts("ARP neighbour and IPv4 route validation tests passed");
    return 0;
}
