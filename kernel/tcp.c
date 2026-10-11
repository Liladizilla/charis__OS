/* tcp.c - Minimal polling TCP client: handshake, data ACKs and close. */
#include <kernel/tcp.h>
#include <kernel/net.h>
#include <kernel/net_stack.h>
#include <kernel/string.h>

#define TCP_HEADER_MIN 20u
#define TCP_SEGMENT_CAP 1460u
#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define TCP_POLL_INTERVAL 4096u
#define TCP_MAX_RETRIES 5u
#define TCP_WINDOW 4096u

typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT
} tcp_state_t;

static tcp_state_t state;
static u8 remote_ip[4];
static u16 remote_port;
static u16 local_port;
static u32 local_seq;
static u32 remote_seq;
static u32 initial_seq;
static u32 poll_ticks;
static u8 retries;
static bool syn_transmitted;
static bool tx_pending;
static bool tx_transmitted;
static bool initialized;
static u32 tx_sequence;
static usize tx_length;
static u8 tx_payload[TCP_SEGMENT_CAP];
static u8 out_segment[TCP_HEADER_MIN + TCP_SEGMENT_CAP];
static net_tcp_connect_callback_t connect_callback;
static net_tcp_data_callback_t data_callback;
static net_tcp_close_callback_t close_callback;
static void* callback_context;
static u16 next_local_port = 49152u;

static u16 read_be16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}
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
static bool ip_equal(const u8* a, const u8* b) {
    for (usize i = 0; i < 4; i++) if (a[i] != b[i]) return false;
    return true;
}
static u16 sum_bytes(u32 sum, const u8* data, usize length) {
    while (length >= 2u) {
        sum += read_be16(data);
        data += 2;
        length -= 2;
    }
    if (length) sum += (u16)((u16)data[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)sum;
}
static u16 tcp_checksum_value(const u8 src[4], const u8 dst[4],
                              const u8* segment, usize length) {
    u32 sum = 0;
    for (usize i = 0; i < 4; i += 2) {
        sum += read_be16(src + i);
        sum += read_be16(dst + i);
    }
    sum += 6u;
    sum += (u32)length;
    return (u16)~sum_bytes(sum, segment, length);
}
static bool tcp_checksum_valid(const u8 src[4], const u8 dst[4],
                               const u8* segment, usize length) {
    return sum_bytes((u32)read_be16(src) + read_be16(src + 2) +
                     read_be16(dst) + read_be16(dst + 2) + 6u +
                     (u32)length, segment, length) == 0xFFFFu;
}

static int emit_segment(u8 flags, u32 sequence, u32 acknowledgement,
                       const u8* payload, usize payload_length) {
    if (payload_length > TCP_SEGMENT_CAP || (!payload && payload_length))
        return -1;
    usize length = TCP_HEADER_MIN + payload_length;
    kmemset(out_segment, 0, length);
    write_be16(out_segment, local_port);
    write_be16(out_segment + 2u, remote_port);
    write_be32(out_segment + 4u, sequence);
    write_be32(out_segment + 8u, acknowledgement);
    out_segment[12] = 0x50u;
    out_segment[13] = flags;
    write_be16(out_segment + 14u, TCP_WINDOW);
    if (payload_length) kmemcpy(out_segment + TCP_HEADER_MIN, payload, payload_length);
    write_be16(out_segment + 16u,
               tcp_checksum_value((const u8*)net_get_interface()->ip_addr,
                                  remote_ip, out_segment, length));
    return net_ipv4_send(remote_ip, 6u, out_segment, length);
}

static void clear_connection(void) {
    state = TCP_CLOSED;
    tx_pending = false;
    tx_transmitted = false;
    syn_transmitted = false;
    tx_length = 0;
    connect_callback = 0;
    data_callback = 0;
    close_callback = 0;
    callback_context = 0;
}

bool net_tcp_init(void) {
    if (initialized) return true;
    state = TCP_CLOSED;
    initialized = true;
    return true;
}

bool net_tcp_connect(const u8 destination_ip[4], u16 destination_port,
                     net_tcp_connect_callback_t on_connect,
                     net_tcp_data_callback_t on_data,
                     net_tcp_close_callback_t on_close, void* context) {
    if (!initialized) (void)net_tcp_init();
    if (!destination_ip || !destination_port || state != TCP_CLOSED ||
        !on_connect)
        return false;
    net_interface_t* iface = net_get_interface();
    if (!iface || !iface->initialized || !(iface->ip_addr[0] ||
        iface->ip_addr[1] || iface->ip_addr[2] || iface->ip_addr[3]))
        return false;

    kmemcpy(remote_ip, destination_ip, 4);
    remote_port = destination_port;
    local_port = next_local_port++;
    if (next_local_port < 49152u) next_local_port = 49152u;
    initial_seq = 0xC4A21503u ^ ((u32)local_port << 16) ^
                  ((u32)destination_port << 1);
    local_seq = initial_seq;
    remote_seq = 0;
    retries = 0;
    poll_ticks = TCP_POLL_INTERVAL - 1u;
    syn_transmitted = false;
    tx_pending = false;
    connect_callback = on_connect;
    data_callback = on_data;
    close_callback = on_close;
    callback_context = context;
    state = TCP_SYN_SENT;
    return true;
}

int net_tcp_send(const void* data, usize length) {
    if (state != TCP_ESTABLISHED || (!data && length) ||
        length == 0 || length > TCP_SEGMENT_CAP)
        return -1;
    if (tx_pending) return -2;
    kmemcpy(tx_payload, data, length);
    tx_length = length;
    tx_sequence = local_seq;
    tx_pending = true;
    tx_transmitted = false;
    retries = 0;
    poll_ticks = TCP_POLL_INTERVAL - 1u;
    int result = emit_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, tx_sequence,
                              remote_seq, tx_payload, tx_length);
    if (result >= 0) {
        tx_transmitted = true;
        local_seq += (u32)tx_length;
        return (int)length;
    }
    if (result == -2) return -2;
    tx_pending = false;
    tx_length = 0;
    return -1;
}

void net_tcp_close(void) {
    if (state == TCP_ESTABLISHED) {
        int result = emit_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, local_seq, remote_seq,
                                  0, 0);
        if (result >= 0) {
            local_seq++;
            state = TCP_FIN_WAIT;
            retries = 0;
            poll_ticks = 0;
            return;
        }
    }
    clear_connection();
}

void net_tcp_input(const u8 source_ip[4], const u8 destination_ip[4],
                   const u8* segment, usize length) {
    if (!initialized || state == TCP_CLOSED || !source_ip || !destination_ip ||
        !segment || !ip_equal(source_ip, remote_ip) ||
        length < TCP_HEADER_MIN)
        return;
    net_interface_t* iface = net_get_interface();
    if (!iface || !ip_equal(destination_ip, iface->ip_addr)) return;
    u8 header_length = (u8)((segment[12] >> 4) * 4u);
    if (header_length < TCP_HEADER_MIN || header_length > length ||
        !tcp_checksum_valid(source_ip, destination_ip, segment, length))
        return;
    if (read_be16(segment) != remote_port || read_be16(segment + 2u) != local_port)
        return;

    u32 sequence = read_be32(segment + 4u);
    u32 acknowledgement = read_be32(segment + 8u);
    u8 flags = segment[13];
    const u8* payload = segment + header_length;
    usize payload_length = length - header_length;

    if (flags & TCP_FLAG_RST) {
        bool was_connecting = state == TCP_SYN_SENT;
        net_tcp_connect_callback_t callback = connect_callback;
        net_tcp_close_callback_t on_close = close_callback;
        void* context = callback_context;
        clear_connection();
        if (was_connecting && callback) callback(false, context);
        else if (on_close) on_close(false, context);
        return;
    }

    if (state == TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) !=
            (TCP_FLAG_SYN | TCP_FLAG_ACK) || acknowledgement != local_seq)
            return;
        remote_seq = sequence + 1u;
        retries = 0;
        /* The transmitted SYN already consumed one local sequence number. */
        state = TCP_ESTABLISHED;
        (void)emit_segment(TCP_FLAG_ACK, local_seq, remote_seq, 0, 0);
        net_tcp_connect_callback_t callback = connect_callback;
        void* context = callback_context;
        connect_callback = 0;
        if (callback) callback(true, context);
        if (payload_length && sequence + 1u == remote_seq) {
            remote_seq += (u32)payload_length;
            if (data_callback) data_callback(payload, payload_length, callback_context);
            (void)emit_segment(TCP_FLAG_ACK, local_seq, remote_seq, 0, 0);
        }
        return;
    }

    /* A cumulative ACK beyond our single outstanding segment is not trusted. */
    if ((flags & TCP_FLAG_ACK) && tx_pending && tx_transmitted &&
        acknowledgement == tx_sequence + (u32)tx_length) {
        tx_pending = false;
        tx_transmitted = false;
        tx_length = 0;
        retries = 0;
    }
    if (payload_length && sequence == remote_seq) {
        remote_seq += (u32)payload_length;
        retries = 0;
        if (data_callback) data_callback(payload, payload_length, callback_context);
        (void)emit_segment(TCP_FLAG_ACK, local_seq, remote_seq, 0, 0);
    }
    if (flags & TCP_FLAG_FIN) {
        /* A FIN is valid only after any payload in this segment is in-order. */
        if (sequence + (u32)payload_length != remote_seq) {
            (void)emit_segment(TCP_FLAG_ACK, local_seq, remote_seq, 0, 0);
            return;
        }
        remote_seq++;
        retries = 0;
        (void)emit_segment(TCP_FLAG_ACK, local_seq, remote_seq, 0, 0);
        net_tcp_close_callback_t on_close = close_callback;
        void* context = callback_context;
        clear_connection();
        if (on_close) on_close(true, context);
        return;
    }
    if (state == TCP_FIN_WAIT && (flags & TCP_FLAG_ACK) &&
        acknowledgement == local_seq) {
        retries = 0;
        clear_connection();
    }
}

void net_tcp_poll(void) {
    if (!initialized || state == TCP_CLOSED) return;
    if (++poll_ticks < TCP_POLL_INTERVAL) return;
    poll_ticks = 0;
    if (retries >= TCP_MAX_RETRIES) {
        bool was_connecting = state == TCP_SYN_SENT;
        net_tcp_connect_callback_t callback = connect_callback;
        net_tcp_close_callback_t on_close = close_callback;
        void* context = callback_context;
        clear_connection();
        if (was_connecting && callback) callback(false, context);
        else if (on_close) on_close(false, context);
        return;
    }
    int result = -1;
    if (state == TCP_SYN_SENT) {
        result = emit_segment(TCP_FLAG_SYN, initial_seq, 0, 0, 0);
        if (result >= 0 && !syn_transmitted) {
            syn_transmitted = true;
            local_seq = initial_seq + 1u;
        }
    } else if (tx_pending && !tx_transmitted) {
        result = emit_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, tx_sequence,
                              remote_seq, tx_payload, tx_length);
        if (result >= 0) {
            tx_transmitted = true;
            local_seq = tx_sequence + (u32)tx_length;
        }
    } else if (tx_pending && tx_transmitted) {
        result = emit_segment(TCP_FLAG_ACK | TCP_FLAG_PSH, tx_sequence,
                              remote_seq, tx_payload, tx_length);
    } else if (state == TCP_FIN_WAIT) {
        result = emit_segment(TCP_FLAG_FIN | TCP_FLAG_ACK, local_seq - 1u,
                              remote_seq, 0, 0);
    }
    if (result != -2) retries++;
}
