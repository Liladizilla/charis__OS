/* net_stack.c - Small polling Ethernet/ARP/IPv4/ICMP foundation.
 *
 * This is intentionally not a complete Internet stack. It implements ARP
 * replies for a configured local IPv4 address, IPv4 header validation, and
 * ICMP echo replies. Routing, DHCP, UDP, TCP, DNS, TLS and HTTP are future work.
 */
#include <kernel/net_stack.h>
#include <kernel/net.h>
#include <kernel/config.h>
#include <kernel/string.h>
#include <kernel/scheduler.h>
#include <kernel/task.h>

#define ETH_HEADER_LEN 14u
#define ETH_TYPE_IPV4  0x0800u
#define ETH_TYPE_ARP   0x0806u
#define ARP_HTYPE_ETH  1u
#define ARP_PTYPE_IPV4 0x0800u
#define ARP_REQUEST    1u
#define ARP_REPLY      2u
#define ARP_CACHE_ENTRIES 16u
#define IPV4_MIN_LEN   20u
#define IPV4_PROTO_ICMP 1u
#define ICMP_ECHO_REPLY 0u
#define ICMP_ECHO_REQUEST 8u
#define NET_FRAME_CAP  NET_MAX_PACKET_SIZE

typedef struct PACKED {
    u8 dest[6], src[6];
    u8 type_hi, type_lo;
} ethernet_header_t;

typedef struct PACKED {
    u8 htype_hi, htype_lo, ptype_hi, ptype_lo;
    u8 hlen, plen, oper_hi, oper_lo;
    u8 sha[6], spa[4], tha[6], tpa[4];
} arp_packet_t;

static u8 local_ip[4];
static u8 frame_in[NET_FRAME_CAP];
static u8 frame_out[NET_FRAME_CAP];
static bool stack_ready;

typedef struct {
    u8 ip[4];
    u8 mac[6];
    bool valid;
} arp_cache_entry_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_ENTRIES];
static u8 arp_cache_next;

static u16 read_be16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}
static void write_be16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}
static bool same_bytes(const u8* a, const u8* b, usize n) {
    for (usize i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}
static bool ip_is_configured(void) {
    return local_ip[0] || local_ip[1] || local_ip[2] || local_ip[3];
}

/* Strict dotted-decimal parser; malformed values leave networking unconfigured. */
static bool parse_ipv4(const char* s, u8 out[4]) {
    if (!s || !s[0]) return false;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return false;
        u32 value = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            value = value * 10u + (u32)(*s - '0');
            if (value > 255u || ++digits > 3) return false;
            s++;
        }
        out[part] = (u8)value;
        if (part < 3) {
            if (*s++ != '.') return false;
        } else if (*s != 0) return false;
    }
    return true;
}

static u16 checksum(const u8* data, usize length) {
    u32 sum = 0;
    while (length >= 2) {
        sum += read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length) sum += (u16)((u16)data[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)~sum;
}

void net_stack_init(void) {
    kmemset(local_ip, 0, sizeof(local_ip));
    kmemset(arp_cache, 0, sizeof(arp_cache));
    arp_cache_next = 0;
    const char* configured = config_get_string("net_ip", "0.0.0.0");
    if (!parse_ipv4(configured, local_ip))
        kmemset(local_ip, 0, sizeof(local_ip));
    net_interface_t* iface = net_get_interface();
    if (iface) kmemcpy(iface->ip_addr, local_ip, sizeof(local_ip));
    stack_ready = true;
}

static bool ip_is_zero(const u8 ip[4]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static bool mac_is_zero(const u8 mac[6]) {
    for (usize i = 0; i < 6; i++) if (mac[i] != 0) return false;
    return true;
}

static void arp_cache_learn(const u8 ip[4], const u8 mac[6]) {
    if (ip_is_zero(ip) || mac_is_zero(mac) || (mac[0] & 1u)) return;
    for (usize i = 0; i < ARP_CACHE_ENTRIES; i++) {
        if (arp_cache[i].valid && same_bytes(arp_cache[i].ip, ip, 4)) {
            kmemcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    arp_cache_entry_t* entry = &arp_cache[arp_cache_next];
    kmemcpy(entry->ip, ip, 4);
    kmemcpy(entry->mac, mac, 6);
    entry->valid = true;
    arp_cache_next = (u8)((arp_cache_next + 1u) % ARP_CACHE_ENTRIES);
}

/* Return a cached neighbour MAC. On a miss, broadcast an ARP request and
 * return false; callers retry after the polling task receives the response. */
bool net_stack_resolve_ipv4(const u8 ip[4], u8 mac_out[6]) {
    if (!stack_ready) net_stack_init();
    if (!ip || !mac_out || !ip_is_configured() || ip_is_zero(ip)) return false;
    for (usize i = 0; i < ARP_CACHE_ENTRIES; i++) {
        if (arp_cache[i].valid && same_bytes(arp_cache[i].ip, ip, 4)) {
            kmemcpy(mac_out, arp_cache[i].mac, 6);
            return true;
        }
    }

    net_interface_t* iface = net_get_interface();
    if (!iface || !iface->initialized) return false;
    kmemset(frame_out, 0, ETH_HEADER_LEN + sizeof(arp_packet_t));
    ethernet_header_t* eth = (ethernet_header_t*)frame_out;
    kmemset(eth->dest, 0xFF, 6);
    kmemcpy(eth->src, iface->mac_addr, 6);
    write_be16(&eth->type_hi, ETH_TYPE_ARP);

    arp_packet_t* req = (arp_packet_t*)(frame_out + ETH_HEADER_LEN);
    req->htype_hi = 0; req->htype_lo = ARP_HTYPE_ETH;
    req->ptype_hi = 0x08; req->ptype_lo = 0x00;
    req->hlen = 6; req->plen = 4;
    req->oper_hi = 0; req->oper_lo = ARP_REQUEST;
    kmemcpy(req->sha, iface->mac_addr, 6);
    kmemcpy(req->spa, local_ip, 4);
    kmemset(req->tha, 0, 6);
    kmemcpy(req->tpa, ip, 4);
    (void)net_send_packet(frame_out, ETH_HEADER_LEN + sizeof(*req));
    return false;
}

static void handle_arp(const ethernet_header_t* eth, usize length) {
    if (length < ETH_HEADER_LEN + sizeof(arp_packet_t) || !ip_is_configured())
        return;
    const arp_packet_t* req = (const arp_packet_t*)((const u8*)eth + ETH_HEADER_LEN);
    u16 operation = read_be16((const u8*)&req->oper_hi);
    if (read_be16((const u8*)&req->htype_hi) != ARP_HTYPE_ETH ||
        read_be16((const u8*)&req->ptype_hi) != ARP_PTYPE_IPV4 ||
        req->hlen != 6 || req->plen != 4 ||
        (operation != ARP_REQUEST && operation != ARP_REPLY))
        return;

    /* Learn valid neighbours from requests and replies. Do not cache zero-IP
     * probes or multicast/broadcast sender hardware addresses. */
    arp_cache_learn(req->spa, req->sha);

    if (operation != ARP_REQUEST || !ip_is_configured() ||
        !same_bytes(req->tpa, local_ip, 4))
        return;

    ethernet_header_t* reply_eth = (ethernet_header_t*)frame_out;
    net_interface_t* iface = net_get_interface();
    if (!iface || !iface->initialized) return;
    kmemcpy(reply_eth->dest, req->sha, 6);
    kmemcpy(reply_eth->src, iface->mac_addr, 6);
    write_be16(&reply_eth->type_hi, ETH_TYPE_ARP);

    arp_packet_t* reply = (arp_packet_t*)(frame_out + ETH_HEADER_LEN);
    reply->htype_hi = 0; reply->htype_lo = ARP_HTYPE_ETH;
    reply->ptype_hi = 0x08; reply->ptype_lo = 0x00;
    reply->hlen = 6; reply->plen = 4;
    reply->oper_hi = 0; reply->oper_lo = ARP_REPLY;
    kmemcpy(reply->sha, iface->mac_addr, 6);
    kmemcpy(reply->spa, local_ip, 4);
    kmemcpy(reply->tha, req->sha, 6);
    kmemcpy(reply->tpa, req->spa, 4);
    (void)net_send_packet(frame_out, ETH_HEADER_LEN + sizeof(*reply));
}

static void handle_icmp(const ethernet_header_t* eth, const u8* ip, usize ip_len) {
    if (ip_len < IPV4_MIN_LEN) return;
    u8 ihl = (u8)((ip[0] & 0x0Fu) * 4u);
    if (ihl < IPV4_MIN_LEN || ihl > ip_len || ip[9] != IPV4_PROTO_ICMP)
        return;
    u16 total = read_be16(ip + 2);
    if (total < ihl + 8u || total > ip_len) return;
    if (!ip_is_configured() || !same_bytes(ip + 16, local_ip, 4)) return;
    if (checksum(ip, ihl) != 0) return;

    const u8* icmp = ip + ihl;
    usize icmp_len = total - ihl;
    if (icmp[0] != ICMP_ECHO_REQUEST || icmp[1] != 0 ||
        checksum(icmp, icmp_len) != 0)
        return;

    /* Reply frame reuses request payload but swaps Ethernet/IP endpoints. */
    ethernet_header_t* out_eth = (ethernet_header_t*)frame_out;
    kmemcpy(out_eth->dest, eth->src, 6);
    kmemcpy(out_eth->src, eth->dest, 6);
    write_be16(&out_eth->type_hi, ETH_TYPE_IPV4);

    u8* out_ip = frame_out + ETH_HEADER_LEN;
    kmemcpy(out_ip, ip, total);
    kmemcpy(out_ip + 12, ip + 16, 4);
    kmemcpy(out_ip + 16, ip + 12, 4);
    out_ip[8] = ip[8] > 1 ? (u8)(ip[8] - 1) : 1;
    out_ip[10] = out_ip[11] = 0;
    write_be16(out_ip + 10, checksum(out_ip, ihl));

    u8* out_icmp = out_ip + ihl;
    out_icmp[0] = ICMP_ECHO_REPLY;
    out_icmp[2] = out_icmp[3] = 0;
    write_be16(out_icmp + 2, checksum(out_icmp, icmp_len));
    (void)net_send_packet(frame_out, ETH_HEADER_LEN + total);
}

static void handle_ipv4(const ethernet_header_t* eth, usize length) {
    if (length < ETH_HEADER_LEN + IPV4_MIN_LEN) return;
    const u8* ip = (const u8*)eth + ETH_HEADER_LEN;
    usize ip_available = length - ETH_HEADER_LEN;
    if ((ip[0] >> 4) != 4) return;
    u8 ihl = (u8)((ip[0] & 0x0Fu) * 4u);
    if (ihl < IPV4_MIN_LEN || ihl > ip_available) return;
    u16 total = read_be16(ip + 2);
    if (total < ihl || total > ip_available) return;
    /* Do not respond to fragmented packets until reassembly is implemented. */
    if (read_be16(ip + 6) & 0x3FFFu) return;
    handle_icmp(eth, ip, total);
}

void net_handle_tcpip(void) {
    if (!stack_ready) net_stack_init();
    int received = net_recv_packet(frame_in, sizeof(frame_in));
    if (received < (int)ETH_HEADER_LEN) return;
    ethernet_header_t* eth = (ethernet_header_t*)frame_in;
    u16 type = read_be16(&eth->type_hi);
    if (type == ETH_TYPE_ARP)
        handle_arp(eth, (usize)received);
    else if (type == ETH_TYPE_IPV4)
        handle_ipv4(eth, (usize)received);
}

/* Runs as a normal kernel task, keeping polling out of interrupt context. */
void net_poll_task(void* unused) {
    (void)unused;
    net_stack_init();
    for (;;) {
        net_handle_tcpip();
        scheduler_yield();
    }
}
