/* Host-side regression test for TCP SYN sequence accounting. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/tcp.c"

static net_interface_t test_iface;
static u8 last_segment[1600];
static usize last_segment_length;
static int connect_calls;
static bool connected_value;

net_interface_t* net_get_interface(void) { return &test_iface; }
int net_ipv4_send(const u8 destination_ip[4], u8 protocol,
                  const void* payload, usize length) {
    (void)destination_ip;
    if (protocol != 6u || length > sizeof(last_segment)) return -1;
    memcpy(last_segment, payload, length);
    last_segment_length = length;
    return (int)length;
}
void* kmemset(void* dst, u8 value, usize length) {
    return memset(dst, value, length);
}
void* kmemcpy(void* dst, const void* src, usize length) {
    return memcpy(dst, src, length);
}
static void connected(bool value, void* context) {
    (void)context;
    connect_calls++;
    connected_value = value;
}
static void put16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}
static void put32(u8* p, u32 value) {
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}
static void expect(bool ok, const char* message) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}
int main(void) {
    memset(&test_iface, 0, sizeof(test_iface));
    test_iface.initialized = true;
    test_iface.ip_addr[0] = 192;
    test_iface.ip_addr[1] = 168;
    test_iface.ip_addr[2] = 1;
    test_iface.ip_addr[3] = 20;
    const u8 server[4] = {203, 0, 113, 9};

    expect(net_tcp_init(), "TCP initialization");
    expect(net_tcp_connect(server, 80u, connected, 0, 0, 0),
           "TCP connection starts");
    net_tcp_poll();
    expect(last_segment_length == 20u && (last_segment[13] & TCP_FLAG_SYN),
           "initial SYN is transmitted");
    u32 expected_next = local_seq;
    expect(expected_next == initial_seq + 1u,
           "sending SYN consumes exactly one local sequence number");

    u8 syn_ack[20];
    memset(syn_ack, 0, sizeof(syn_ack));
    put16(syn_ack, 80u);
    put16(syn_ack + 2u, local_port);
    put32(syn_ack + 4u, 7000u);
    put32(syn_ack + 8u, expected_next);
    syn_ack[12] = 0x50u;
    syn_ack[13] = TCP_FLAG_SYN | TCP_FLAG_ACK;
    put16(syn_ack + 14u, 4096u);
    put16(syn_ack + 16u,
           tcp_checksum_value(server, test_iface.ip_addr, syn_ack, sizeof(syn_ack)));
    net_tcp_input(server, test_iface.ip_addr, syn_ack, sizeof(syn_ack));

    expect(state == TCP_ESTABLISHED && connect_calls == 1 && connected_value,
           "valid SYN/ACK completes the handshake");
    expect(local_seq == expected_next,
           "accepting SYN/ACK must not increment local sequence a second time");

    expect(net_tcp_send("x", 1u) == 1, "first data byte is sent");
    expect(read_be32(last_segment + 4u) == expected_next,
           "first data segment starts at the expected post-SYN sequence");
    puts("TCP SYN sequence regression test passed");
    return 0;
}
