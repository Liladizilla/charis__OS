/* Host-side regressions for authenticated DNS response parsing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/dns.c"

static int callback_count;
static bool callback_success;
static u8 callback_ip[4];

static void capture_dns(const char* hostname, const u8 ip[4], bool success,
                        void* context) {
    (void)hostname;
    (void)context;
    callback_count++;
    callback_success = success;
    memcpy(callback_ip, ip, 4);
}

static void reset_dns_test(void) {
    memset(dns_server, 0, sizeof(dns_server));
    dns_server[0] = 1; dns_server[1] = 1;
    dns_server[2] = 1; dns_server[3] = 1;
    memset(pending_host, 0, sizeof(pending_host));
    strcpy(pending_host, "example.com");
    pending_id = 0x1234u;
    active = true;
    pending_callback = capture_dns;
    pending_context = NULL;
    callback_count = 0;
    callback_success = false;
    memset(callback_ip, 0, sizeof(callback_ip));
}

static void put16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

/* Standard example.com A/IN response with a compressed owner name. */
static usize build_response(u8* p) {
    memset(p, 0, 128);
    put16(p, 0x1234u);
    put16(p + 2u, 0x8180u);
    put16(p + 4u, 1u);
    put16(p + 6u, 1u);
    usize n = 12u;
    p[n++] = 7u;
    memcpy(p + n, "example", 7u); n += 7u;
    p[n++] = 3u;
    memcpy(p + n, "com", 3u); n += 3u;
    p[n++] = 0u;
    put16(p + n, 1u); n += 2u; /* QTYPE A */
    put16(p + n, 1u); n += 2u; /* QCLASS IN */
    p[n++] = 0xC0u; p[n++] = 0x0Cu; /* answer owner -> example.com */
    put16(p + n, 1u); n += 2u;
    put16(p + n, 1u); n += 2u;
    n += 4u; /* TTL */
    put16(p + n, 4u); n += 2u;
    p[n++] = 93u; p[n++] = 184u; p[n++] = 216u; p[n++] = 34u;
    return n;
}

static void expect(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void test_valid_compressed_question_and_answer(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && callback_success,
           "matching compressed A/IN response must succeed");
    expect(callback_ip[0] == 93u && callback_ip[1] == 184u &&
           callback_ip[2] == 216u && callback_ip[3] == 34u,
           "matching A record address");
}

static void test_wrong_echoed_hostname_is_rejected(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet);
    packet[23] = (u8)'t'; /* example.com -> example.cot */
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "wrong echoed QNAME must be rejected");
}

static void test_wrong_question_type_or_class_is_rejected(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet);
    put16(packet + 25u, 28u); /* AAAA, not A */
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "AAAA question must not satisfy A resolver");

    reset_dns_test();
    n = build_response(packet);
    put16(packet + 27u, 3u); /* CH, not IN */
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "non-IN question must be rejected");
}

static void test_multiple_questions_and_nonstandard_opcode_are_rejected(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet);
    put16(packet + 4u, 2u);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "multiple DNS questions must be rejected");

    reset_dns_test();
    n = build_response(packet);
    put16(packet + 2u, 0x8980u); /* response with nonzero opcode */
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "nonstandard DNS opcode must be rejected");
}

static void test_compression_loops_and_forward_pointers_are_rejected(void) {
    u8 packet[128] = {0};
    reset_dns_test();
    put16(packet, pending_id);
    put16(packet + 2u, 0x8180u);
    put16(packet + 4u, 1u);
    packet[12] = 0xC0u; packet[13] = 0x0Cu; /* self-reference */
    handle_dns_response(53u, dns_server, packet, 14u);
    expect(callback_count == 1 && !callback_success,
           "self-referential compression pointer must be rejected");

    reset_dns_test();
    memset(packet, 0, sizeof(packet));
    put16(packet, pending_id);
    put16(packet + 2u, 0x8180u);
    put16(packet + 4u, 1u);
    packet[12] = 0xC0u; packet[13] = 0x00u; /* points into DNS header */
    handle_dns_response(53u, dns_server, packet, 14u);
    expect(callback_count == 1 && !callback_success,
           "compression pointer into DNS header must be rejected");

    reset_dns_test();
    memset(packet, 0, sizeof(packet));
    put16(packet, pending_id);
    put16(packet + 2u, 0x8180u);
    put16(packet + 4u, 1u);
    packet[12] = 0xC0u; packet[13] = 0x20u; /* points beyond packet */
    handle_dns_response(53u, dns_server, packet, 14u);
    expect(callback_count == 1 && !callback_success,
           "out-of-range compression pointer must be rejected");
}

static void test_unrelated_answer_owner_is_not_accepted(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet);
    packet[29] = 0xC0u;
    packet[30] = 0x14u; /* points to the "com" label only */
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "A record for unrelated owner must not satisfy query");
}

static void test_wrong_source_is_ignored_and_zero_address_is_rejected(void) {
    u8 packet[128];
    u8 wrong_source[4] = {8u, 8u, 8u, 8u};
    reset_dns_test();
    usize n = build_response(packet);
    handle_dns_response(53u, wrong_source, packet, n);
    expect(callback_count == 0 && active,
           "wrong DNS source must not complete query");

    reset_dns_test();
    n = build_response(packet);
    memset(packet + n - 4u, 0, 4u);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success,
           "unspecified A record address must be rejected");
}

int main(void) {
    test_valid_compressed_question_and_answer();
    test_wrong_echoed_hostname_is_rejected();
    test_wrong_question_type_or_class_is_rejected();
    test_multiple_questions_and_nonstandard_opcode_are_rejected();
    test_compression_loops_and_forward_pointers_are_rejected();
    test_unrelated_answer_owner_is_not_accepted();
    test_wrong_source_is_ignored_and_zero_address_is_rejected();
    puts("DNS response authentication tests passed");
    return 0;
}

/* Kernel API stubs: response parsing does not require a network device. */
void* kmemset(void* d, u8 v, usize n) { return memset(d, v, n); }
void* kmemcpy(void* d, const void* s, usize n) { return memcpy(d, s, n); }
void* kmemmove(void* d, const void* s, usize n) { return memmove(d, s, n); }
char* kstrcpy(char* d, const char* s) { return strcpy(d, s); }
char* kstrncpy(char* d, const char* s, usize n) { return strncpy(d, s, n); }
usize kstrlen(const char* s) { return strlen(s); }
int kstrcmp(const char* a, const char* b) { return strcmp(a, b); }
int kstrncmp(const char* a, const char* b, usize n) { return strncmp(a, b, n); }
char* kstrchr(const char* s, char c) { return (char*)strchr(s, c); }
char* kstrrchr(const char* s, char c) { return (char*)strrchr(s, c); }
char* kstrstr(const char* h, const char* n) { return (char*)strstr(h, n); }
const char* config_get_string(const char* key, const char* fallback) {
    (void)key;
    return fallback;
}
bool net_udp_bind(u16 port, net_udp_handler_t handler) {
    (void)port; (void)handler;
    return true;
}
int net_udp_send(const u8 ip[4], u16 destination_port, u16 source_port,
                 const void* data, usize length) {
    (void)ip; (void)destination_port; (void)source_port; (void)data; (void)length;
    return 0;
}
