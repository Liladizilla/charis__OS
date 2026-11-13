/* Host-side regression tests for the bounded HTTP response parser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/http.c"

static char captured[128];
static usize captured_length;
static int completion_calls;
static bool completion_success;

static void test_body(const u8* data, usize length, void* context) {
    (void)context;
    if (captured_length + length >= sizeof(captured)) abort();
    memcpy(captured + captured_length, data, length);
    captured_length += length;
    captured[captured_length] = 0;
}
static void test_complete(bool success, void* context) {
    (void)context;
    completion_calls++;
    completion_success = success;
}
static void reset_parser(void) {
    active = true;
    headers_done = false;
    has_content_length = false;
    chunked_encoding = false;
    chunk_state = HTTP_CHUNK_SIZE;
    chunk_line_length = 0;
    chunk_remaining = 0;
    chunk_trailer_bytes = 0;
    response_status = 0;
    expected_body = 0;
    received_body = 0;
    header_length = 0;
    captured_length = 0;
    captured[0] = 0;
    completion_calls = 0;
    completion_success = false;
    headers_callback = 0;
    body_callback = test_body;
    complete_callback = test_complete;
    request_context = 0;
}
static void expect(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}
static void feed_split(const char* response, usize split) {
    usize total = strlen(response);
    if (split > total) split = total;
    http_on_data((const u8*)response, split, 0);
    if (split < total)
        http_on_data((const u8*)response + split, total - split, 0);
}
static void test_chunked_split_at_every_boundary(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked \t\r\n"
        "Connection: close\r\n\r\n"
        "4\r\nWiki\r\n"
        "5;foo=bar\r\npedia\r\n"
        "0\r\nX-Long-Trailer: accepted\r\n\r\n";
    usize total = sizeof(response) - 1u;
    for (usize split = 0; split <= total; split++) {
        reset_parser();
        feed_split(response, split);
        expect(!active, "complete chunked response should finish");
        expect(completion_calls == 1 && completion_success,
               "chunked response should complete successfully");
        expect(strcmp(captured, "Wikipedia") == 0,
               "chunk boundaries and extensions should be removed");
    }
}
static void test_chunked_one_byte_at_a_time(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabc\r\n2\r\nde\r\n0\r\n\r\n";
    reset_parser();
    for (usize i = 0; i < sizeof(response) - 1u; i++)
        http_on_data((const u8*)response + i, 1, 0);
    expect(!active && completion_calls == 1 && completion_success,
           "one-byte network segments should decode correctly");
    expect(strcmp(captured, "abcde") == 0, "one-byte chunk stream body");
}
static void test_malformed_chunk_is_rejected(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "3\r\nabcXX0\r\n\r\n";
    reset_parser();
    http_on_data((const u8*)response, sizeof(response) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "bad chunk delimiter must fail");
}
static void test_unsupported_transfer_encoding_is_rejected(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nbody";
    reset_parser();
    http_on_data((const u8*)response, sizeof(response) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "unsupported transfer encoding must fail closed");
}
static void test_invalid_and_overflow_chunk_sizes_are_rejected(void) {
    static const char invalid[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "Z\r\nnope\r\n0\r\n\r\n";
    static const char overflow[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "100000000\r\n";
    reset_parser();
    http_on_data((const u8*)invalid, sizeof(invalid) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "non-hex chunk size must fail");
    reset_parser();
    http_on_data((const u8*)overflow, sizeof(overflow) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "overflowing chunk size must fail");
}

static void test_content_length_response(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhelloEXTRA";
    reset_parser();
    http_on_data((const u8*)response, sizeof(response) - 1u, 0);
    expect(!active && completion_calls == 1 && completion_success,
           "content-length response should complete");
    expect(strcmp(captured, "hello") == 0,
           "bytes beyond content-length must not reach body callback");
}
static void test_conflicting_framing_is_rejected(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n"
        "Transfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
    reset_parser();
    http_on_data((const u8*)response, sizeof(response) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "content-length plus transfer-encoding must be rejected");
}
static void test_no_body_status_completes_at_headers(void) {
    static const char response[] =
        "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
    reset_parser();
    http_on_data((const u8*)response, sizeof(response) - 1u, 0);
    expect(!active && completion_calls == 1 && completion_success,
           "204 response should finish after headers");
    expect(captured_length == 0, "204 response must not deliver body");
}
static void test_truncated_chunk_is_not_success(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5\r\nabc";
    reset_parser();
    http_on_data((const u8*)response, sizeof(response) - 1u, 0);
    http_on_closed(true, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "truncated chunked response must fail");
}

static void test_body_and_trailer_limits(void) {
    char response[4096];
    const char* chunk_prefix =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    reset_parser();
    int n = snprintf(response, sizeof(response), "%s40001\r\n", chunk_prefix);
    expect(n > 0 && (usize)n < sizeof(response), "large chunk response formatting");
    http_on_data((const u8*)response, (usize)n, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "chunk exceeding decoded body limit must fail before delivery");

    reset_parser();
    n = snprintf(response, sizeof(response), "%s0\r\n", chunk_prefix);
    expect(n > 0 && (usize)n < sizeof(response), "trailer response formatting");
    usize used = (usize)n;
    for (u32 i = 0; i < 16u; i++) {
        int written = snprintf(response + used, sizeof(response) - used,
                               "X-Padding-%u: ", i);
        expect(written > 0 && (usize)written < sizeof(response) - used,
               "trailer line formatting");
        used += (usize)written;
        if (used + 82u >= sizeof(response)) abort();
        memset(response + used, 'x', 80u);
        used += 80u;
        response[used++] = '\r';
        response[used++] = '\n';
    }
    response[used++] = '\r';
    response[used++] = '\n';
    http_on_data((const u8*)response, used, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "aggregate trailer bytes must be bounded");
}

static void test_invalid_trailer_fields_are_rejected(void) {
    static const char leading_space[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "0\r\n Content-Length: 4\r\n\r\n";
    static const char framing_trailer[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "0\r\nContent-Length: 4\r\n\r\n";
    reset_parser();
    http_on_data((const u8*)leading_space, sizeof(leading_space) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "obsolete folded trailer lines must be rejected");
    reset_parser();
    http_on_data((const u8*)framing_trailer, sizeof(framing_trailer) - 1u, 0);
    expect(!active && completion_calls == 1 && !completion_success,
           "framing fields are not accepted in trailers");
}

int main(void) {
    test_chunked_split_at_every_boundary();
    test_chunked_one_byte_at_a_time();
    test_malformed_chunk_is_rejected();
    test_unsupported_transfer_encoding_is_rejected();
    test_invalid_and_overflow_chunk_sizes_are_rejected();
    test_content_length_response();
    test_conflicting_framing_is_rejected();
    test_no_body_status_completes_at_headers();
    test_truncated_chunk_is_not_success();
    test_body_and_trailer_limits();
    test_invalid_trailer_fields_are_rejected();
    puts("HTTP chunked parser tests passed");
    return 0;
}

/* Kernel API stubs: parser tests don't need a live network stack. */
void* kmemset(void* dst, u8 val, usize n) { return memset(dst, val, n); }
void* kmemcpy(void* dst, const void* src, usize n) { return memcpy(dst, src, n); }
void* kmemmove(void* dst, const void* src, usize n) { return memmove(dst, src, n); }
char* kstrcpy(char* dst, const char* src) { return strcpy(dst, src); }
char* kstrncpy(char* dst, const char* src, usize n) { return strncpy(dst, src, n); }
usize kstrlen(const char* s) { return strlen(s); }
int kstrcmp(const char* a, const char* b) { return strcmp(a, b); }
int kstrncmp(const char* a, const char* b, usize n) { return strncmp(a, b, n); }
char* kstrchr(const char* s, char c) { return (char*)strchr(s, c); }
char* kstrrchr(const char* s, char c) { return (char*)strrchr(s, c); }
char* kstrstr(const char* h, const char* n) { return (char*)strstr(h, n); }
bool net_dns_query(const char* h, net_dns_callback_t cb, void* ctx) {
    (void)h; (void)cb; (void)ctx; return true;
}
bool net_tcp_connect(const u8 ip[4], u16 port, net_tcp_connect_callback_t on_connect,
                     net_tcp_data_callback_t on_data, net_tcp_close_callback_t on_close,
                     void* ctx) {
    (void)ip; (void)port; (void)on_connect; (void)on_data; (void)on_close; (void)ctx;
    return true;
}
int net_tcp_send(const void* data, usize length) { (void)data; return (int)length; }
void net_tcp_close(void) {}
