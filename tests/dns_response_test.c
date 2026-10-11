/* Host-side regression tests for DNS response validation. */
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
    dns_server[0] = 1; dns_server[1] = 1; dns_server[2] = 1; dns_server[3] = 1;
    memset(pending_host, 0, sizeof(pending_host));
    strcpy(pending_host, "example.com");
    pending_id = 0x1234u;
    active = true;
    pending_callback = capture_dns;
    pending_context = 0;
    callback_count = 0;
    callback_success = false;
    memset(callback_ip, 0, sizeof(callback_ip));
}
static void put16(u8* p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static usize build_response(u8* p, const char* second_label,
                            u16 question_type, u16 question_count) {
    memset(p, 0, 128);
    put16(p, 0x1234u);
    put16(p + 2, 0x8180u);
    put16(p + 4, question_count);
    put16(p + 6, 1u);
    usize n = 12;
    p[n++] = 7;
    memcpy(p + n, "example", 7); n += 7;
    usize label_len = strlen(second_label);
    p[n++] = (u8)label_len;
    memcpy(p + n, second_label, label_len); n += label_len;
    p[n++] = 0;
    put16(p + n, question_type); n += 2;
    put16(p + n, 1u); n += 2;
    p[n++] = 0xC0; p[n++] = 0x0C; /* compressed answer owner */
    put16(p + n, 1u); n += 2;
    put16(p + n, 1u); n += 2;
    n += 4; /* TTL */
    put16(p + n, 4u); n += 2;
    p[n++] = 93; p[n++] = 184; p[n++] = 216; p[n++] = 34;
    return n;
}
static void expect(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void test_valid_matching_question(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet, "com", 1u, 1u);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && callback_success, "matching A/IN answer succeeds");
    expect(callback_ip[0] == 93 && callback_ip[1] == 184 &&
           callback_ip[2] == 216 && callback_ip[3] == 34, "A record address");
}
static void test_wrong_hostname_is_rejected(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet, "net", 1u, 1u);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success, "wrong echoed hostname rejected");
}
static void test_wrong_question_type_is_rejected(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet, "com", 28u, 1u);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success, "AAAA question rejected by A resolver");
}
static void test_multiple_questions_are_rejected(void) {
    u8 packet[128];
    reset_dns_test();
    usize n = build_response(packet, "com", 1u, 2u);
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success, "multiple questions rejected");
}
static void test_bad_compression_pointer_is_rejected(void) {
    u8 packet[32] = {0};
    reset_dns_test();
    put16(packet, pending_id);
    put16(packet + 2, 0x8180u);
    put16(packet + 4, 1u);
    put16(packet + 6, 0u);
    packet[12] = 0xC0; packet[13] = 0x0C; /* self-referential pointer */
    usize n = 14;
    handle_dns_response(53u, dns_server, packet, n);
    expect(callback_count == 1 && !callback_success, "compression pointer loop rejected");
}
static void test_wrong_source_is_ignored(void) {
    u8 packet[128];
    u8 wrong_source[4] = {8, 8, 8, 8};
    reset_dns_test();
    usize n = build_response(packet, "com", 1u, 1u);
    handle_dns_response(53u, wrong_source, packet, n);
    expect(callback_count == 0 && active, "wrong DNS source must not complete query");
}
int main(void) {
    test_valid_matching_question();
    test_wrong_hostname_is_rejected();
    test_wrong_question_type_is_rejected();
    test_multiple_questions_are_rejected();
    test_bad_compression_pointer_is_rejected();
    test_wrong_source_is_ignored();
    puts("DNS response validation tests passed");
    return 0;
}

/* Kernel API stubs: DNS parsing tests do not require a network device. */
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
    (void)key; return fallback;
}
bool net_udp_bind(u16 port, net_udp_handler_t handler) {
    (void)port; (void)handler; return true;
}
int net_udp_send(const u8 ip[4], u16 dp, u16 sp, const void* data, usize n) {
    (void)ip; (void)dp; (void)sp; (void)data; (void)n; return 0;
}
