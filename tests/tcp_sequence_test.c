/* Host-side regression tests for TCP sequence, ACK and FIN handling. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/tcp.c"

static net_interface_t test_iface;
static u8 last_segment[1600];
static usize last_segment_length;
static int connect_calls;
static bool connected_value;
static int close_calls;
static bool close_clean;
static const u8 server[4] = {203, 0, 113, 9};

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
static void closed(bool clean, void* context) {
    (void)context;
    close_calls++;
    close_clean = clean;
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
static usize make_segment(u8* segment, u32 sequence, u32 acknowledgement,
                          u8 flags, const u8* payload, usize payload_length) {
    usize length = 20u + payload_length;
    memset(segment, 0, length);
    put16(segment, 80u);
    put16(segment + 2u, local_port);
    put32(segment + 4u, sequence);
    put32(segment + 8u, acknowledgement);
    segment[12] = 0x50u;
    segment[13] = flags;
    put16(segment + 14u, 4096u);
    if (payload_length) memcpy(segment + 20u, payload, payload_length);
    put16(segment + 16u,
          tcp_checksum_value(server, test_iface.ip_addr, segment, length));
    return length;
}
int main(void) {
    memset(&test_iface, 0, sizeof(test_iface));
    test_iface.initialized = true;
    test_iface.ip_addr[0] = 192;
    test_iface.ip_addr[1] = 168;
    test_iface.ip_addr[2] = 1;
    test_iface.ip_addr[3] = 20;

    expect(net_tcp_init(), "TCP initialization");
    expect(net_tcp_connect(server, 80u, connected, 0, closed, 0),
           "TCP connection starts");
    net_tcp_poll();
    expect(last_segment_length == 20u && (last_segment[13] & TCP_FLAG_SYN),
           "initial SYN is transmitted");
    u32 expected_next = local_seq;
    expect(expected_next == initial_seq + 1u,
           "sending SYN consumes exactly one local sequence number");

    u8 incoming[1600];
    usize length = make_segment(incoming, 7000u, expected_next,
                                TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
    net_tcp_input(server, test_iface.ip_addr, incoming, length);
    expect(state == TCP_ESTABLISHED && connect_calls == 1 && connected_value,
           "valid SYN/ACK completes the handshake");
    expect(local_seq == expected_next,
           "accepting SYN/ACK must not increment local sequence a second time");

    expect(net_tcp_send("hello", 5u) == 5,
           "first data segment is transmitted");
    u32 data_sequence = read_be32(last_segment + 4u);
    expect(data_sequence == expected_next,
           "first data segment starts at the expected post-SYN sequence");
    expect(tx_pending, "data remains pending until acknowledged");

    length = make_segment(incoming, remote_seq, data_sequence + 4u,
                          TCP_FLAG_ACK, 0, 0);
    net_tcp_input(server, test_iface.ip_addr, incoming, length);
    expect(tx_pending, "partial ACK does not clear the whole segment");

    length = make_segment(incoming, remote_seq, local_seq + 20u,
                          TCP_FLAG_ACK, 0, 0);
    net_tcp_input(server, test_iface.ip_addr, incoming, length);
    expect(tx_pending, "future ACK cannot acknowledge unsent sequence space");

    length = make_segment(incoming, remote_seq, data_sequence + 5u,
                          TCP_FLAG_ACK, 0, 0);
    net_tcp_input(server, test_iface.ip_addr, incoming, length);
    expect(!tx_pending, "exact ACK clears the outstanding data segment");

    length = make_segment(incoming, remote_seq + 3u, local_seq,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    net_tcp_input(server, test_iface.ip_addr, incoming, length);
    expect(state == TCP_ESTABLISHED && close_calls == 0,
           "out-of-order FIN does not close the connection");

    length = make_segment(incoming, remote_seq, local_seq,
                          TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
    net_tcp_input(server, test_iface.ip_addr, incoming, length);
    expect(state == TCP_CLOSED && close_calls == 1 && close_clean,
           "in-order FIN closes the connection cleanly");
    puts("TCP sequence, ACK and FIN regression tests passed");
    return 0;
}
