/* dns.c - Bounded asynchronous DNS A-record lookup over UDP/IPv4. */
#include <kernel/dns.h>
#include <kernel/net_stack.h>
#include <kernel/config.h>
#include <kernel/string.h>

#define DNS_PORT 53u
#define DNS_SOURCE_PORT 53000u
#define DNS_PACKET_MAX 512u
#define DNS_HOST_MAX 253u
#define DNS_POLL_INTERVAL 4096u
#define DNS_MAX_ATTEMPTS 5u

static u8 dns_server[4];
static u8 query_packet[DNS_PACKET_MAX];
static usize query_length;
static char pending_host[DNS_HOST_MAX + 1u];
static u16 next_id = 0x4D31u;
static u16 pending_id;
static u32 poll_ticks;
static u8 attempts;
static bool ready;
static bool active;
static net_dns_callback_t pending_callback;
static void* pending_context;

static u16 read_be16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}
static void write_be16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}
static bool ip_equal(const u8* a, const u8* b) {
    for (usize i = 0; i < 4; i++) if (a[i] != b[i]) return false;
    return true;
}
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

/* Encodes labels without accepting empty labels, overlong labels or bad dots. */
static bool encode_hostname(const char* host, u8* out, usize cap,
                            usize* encoded_length) {
    if (!host || !out || !encoded_length) return false;
    usize length = kstrlen(host);
    if (!length || length > DNS_HOST_MAX || cap < length + 2u) return false;
    usize out_pos = 0, label_start = 0, label_length = 0;
    for (usize i = 0; i <= length; i++) {
        char ch = host[i];
        if (ch == '.' || ch == 0) {
            label_length = i - label_start;
            if (!label_length || label_length > 63u) return false;
            out[out_pos++] = (u8)label_length;
            for (usize j = label_start; j < i; j++) {
                char c = host[j];
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-'))
                    return false;
                out[out_pos++] = (u8)c;
            }
            label_start = i + 1u;
        }
    }
    if (out_pos + 1u > cap) return false;
    out[out_pos++] = 0;
    *encoded_length = out_pos;
    return true;
}

/* Compare the echoed DNS question against the requested hostname. Compression
 * pointers are followed with a strict step limit to reject loops. */
static bool question_matches_host(const u8* packet, usize length, usize* offset) {
    usize pos = *offset;
    usize consumed = 0;
    usize host_pos = 0;
    bool jumped = false;
    for (usize steps = 0; steps < 128u; steps++) {
        if (pos >= length) return false;
        u8 label = packet[pos++];
        if ((label & 0xC0u) == 0xC0u) {
            if (pos >= length) return false;
            usize pointer = ((usize)(label & 0x3Fu) << 8) | packet[pos++];
            if (pointer >= length) return false;
            if (!jumped) consumed = pos;
            jumped = true;
            pos = pointer;
            continue;
        }
        if (label & 0xC0u) return false;
        if (label == 0) {
            if (pending_host[host_pos] != 0) return false;
            *offset = jumped ? consumed : pos;
            return true;
        }
        if (label > 63u || (usize)label > length - pos) return false;
        for (u8 i = 0; i < label; i++) {
            if (!pending_host[host_pos]) return false;
            char expected = pending_host[host_pos++];
            char actual = (char)packet[pos++];
            if (expected >= 'A' && expected <= 'Z')
                expected = (char)(expected - 'A' + 'a');
            if (actual >= 'A' && actual <= 'Z')
                actual = (char)(actual - 'A' + 'a');
            if (expected != actual) return false;
        }
        if (pending_host[host_pos] == '.') host_pos++;
        else if (pending_host[host_pos] != 0) return false;
    }
    return false;
}

/* Skip an encoded DNS name, including compressed names, without following
 * pointers. This is sufficient for locating the next fixed-size field. */
static bool skip_name(const u8* packet, usize length, usize* offset) {
    usize pos = *offset;
    for (usize steps = 0; steps < 128u; steps++) {
        if (pos >= length) return false;
        u8 label = packet[pos++];
        if ((label & 0xC0u) == 0xC0u) {
            if (pos >= length) return false;
            pos++;
            *offset = pos;
            return true;
        }
        if (label & 0xC0u) return false;
        if (label == 0) {
            *offset = pos;
            return true;
        }
        if ((usize)label > length - pos) return false;
        pos += label;
    }
    return false;
}

static void finish_query(bool success, const u8 ip[4]) {
    net_dns_callback_t callback = pending_callback;
    void* context = pending_context;
    active = false;
    pending_callback = 0;
    pending_context = 0;
    if (callback) callback(pending_host, ip, success, context);
}

static void handle_dns_response(u16 source_port, const u8 source_ip[4],
                                const u8* payload, usize length) {
    if (!active || source_port != DNS_PORT || !ip_equal(source_ip, dns_server) ||
        !payload || length < 12u || read_be16(payload) != pending_id)
        return;

    u16 flags = read_be16(payload + 2);
    u16 questions = read_be16(payload + 4);
    u16 answers = read_be16(payload + 6);
    if (!(flags & 0x8000u) || (flags & 0x7800u) != 0 ||
        (flags & 0x0200u) || (flags & 0x000Fu) != 0 || questions != 1u) {
        const u8 zero[4] = {0, 0, 0, 0};
        finish_query(false, zero);
        return;
    }

    usize offset = 12u;
    if (!question_matches_host(payload, length, &offset) ||
        offset + 4u > length || read_be16(payload + offset) != 1u ||
        read_be16(payload + offset + 2u) != 1u) {
        const u8 zero[4] = {0, 0, 0, 0};
        finish_query(false, zero);
        return;
    }
    offset += 4u;

    for (u16 i = 0; i < answers; i++) {
        usize owner_end = offset;
        bool owner_matches = question_matches_host(payload, length, &owner_end);
        if (owner_matches) {
            offset = owner_end;
        } else if (!skip_name(payload, length, &offset)) {
            const u8 zero[4] = {0, 0, 0, 0};
            finish_query(false, zero);
            return;
        }
        if (offset + 10u > length) {
            const u8 zero[4] = {0, 0, 0, 0};
            finish_query(false, zero);
            return;
        }
        u16 type = read_be16(payload + offset);
        u16 klass = read_be16(payload + offset + 2u);
        u16 rdlength = read_be16(payload + offset + 8u);
        offset += 10u;
        if ((usize)rdlength > length - offset) {
            const u8 zero[4] = {0, 0, 0, 0};
            finish_query(false, zero);
            return;
        }
        if (owner_matches && type == 1u && klass == 1u && rdlength == 4u) {
            u8 ip[4];
            kmemcpy(ip, payload + offset, 4);
            if (ip[0] || ip[1] || ip[2] || ip[3]) {
                finish_query(true, ip);
                return;
            }
        }
        offset += rdlength;
    }
    const u8 zero[4] = {0, 0, 0, 0};
    finish_query(false, zero);
}

bool net_dns_init(void) {
    if (ready) return true;
    if (!parse_ipv4(config_get_string("net_dns", "1.1.1.1"), dns_server))
        return false;
    if (!(dns_server[0] || dns_server[1] || dns_server[2] || dns_server[3]))
        return false;
    if (!net_udp_bind(DNS_PORT, handle_dns_response)) return false;
    ready = true;
    return true;
}

bool net_dns_query(const char* hostname, net_dns_callback_t callback,
                   void* context) {
    if (!ready || active || !hostname || !callback) return false;
    usize host_len = kstrlen(hostname);
    if (!host_len || host_len > DNS_HOST_MAX) return false;
    kmemset(query_packet, 0, sizeof(query_packet));
    usize encoded = 0;
    if (!encode_hostname(hostname, query_packet + 12u,
                         sizeof(query_packet) - 16u, &encoded))
        return false;

    pending_id = next_id++;
    if (!next_id) next_id = 1;
    write_be16(query_packet, pending_id);
    write_be16(query_packet + 2u, 0x0100u); /* standard query, recursion desired */
    write_be16(query_packet + 4u, 1u);
    write_be16(query_packet + 6u, 0u);
    write_be16(query_packet + 8u, 0u);
    write_be16(query_packet + 10u, 0u);
    usize question_end = 12u + encoded;
    write_be16(query_packet + question_end, 1u); /* A */
    write_be16(query_packet + question_end + 2u, 1u); /* IN */
    query_length = question_end + 4u;

    kstrncpy(pending_host, hostname, sizeof(pending_host));
    pending_host[DNS_HOST_MAX] = 0;
    pending_callback = callback;
    pending_context = context;
    attempts = 0;
    poll_ticks = DNS_POLL_INTERVAL - 1u;
    active = true;
    return true;
}

void net_dns_poll(void) {
    if (!active) return;
    if (++poll_ticks < DNS_POLL_INTERVAL) return;
    poll_ticks = 0;
    if (attempts >= DNS_MAX_ATTEMPTS) {
        const u8 zero[4] = {0, 0, 0, 0};
        finish_query(false, zero);
        return;
    }
    int sent = net_udp_send(dns_server, DNS_PORT, DNS_SOURCE_PORT,
                            query_packet, query_length);
    /* -2 means ARP resolution started. Retrying after an interval allows the
     * normal packet poller to receive the reply without flooding ARP. */
    (void)sent;
    attempts++;
}
