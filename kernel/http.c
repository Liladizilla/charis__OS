/* http.c - Bounded HTTP/1.x GET client over the early DNS/TCP stack. */
#include <kernel/http.h>
#include <kernel/dns.h>
#include <kernel/tcp.h>
#include <kernel/string.h>

#define HTTP_HOST_MAX 253u
#define HTTP_PATH_MAX 512u
#define HTTP_HEADER_MAX 2048u
#define HTTP_REQUEST_MAX 1024u
#define HTTP_CHUNK_LINE_MAX 128u

typedef enum {
    HTTP_CHUNK_SIZE,
    HTTP_CHUNK_DATA,
    HTTP_CHUNK_DATA_CR,
    HTTP_CHUNK_DATA_LF,
    HTTP_CHUNK_TRAILER,
    HTTP_CHUNK_DONE
} http_chunk_state_t;

static bool active;
static bool headers_done;
static bool has_content_length;
static bool chunked_encoding;
static http_chunk_state_t chunk_state;
static char chunk_line[HTTP_CHUNK_LINE_MAX];
static usize chunk_line_length;
static u32 chunk_remaining;
static u16 response_status;
static u32 expected_body;
static u32 received_body;
static char request_host[HTTP_HOST_MAX + 1u];
static char request_path[HTTP_PATH_MAX + 1u];
static char request[HTTP_REQUEST_MAX];
static usize request_length;
static u8 header_buffer[HTTP_HEADER_MAX + 1u];
static usize header_length;
static net_http_headers_callback_t headers_callback;
static net_http_body_callback_t body_callback;
static net_http_complete_callback_t complete_callback;
static void* request_context;

static bool ascii_equal(const char* a, usize a_len, const char* b) {
    usize b_len = kstrlen(b);
    if (a_len != b_len) return false;
    for (usize i = 0; i < a_len; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

static bool append_text(usize* offset, const char* text) {
    usize length = kstrlen(text);
    if (*offset + length >= sizeof(request)) return false;
    kmemcpy(request + *offset, text, length);
    *offset += length;
    request[*offset] = 0;
    return true;
}

static void finish_request(bool success, bool close_tcp) {
    if (!active) return;
    active = false;
    net_http_complete_callback_t callback = complete_callback;
    void* context = request_context;
    complete_callback = 0;
    headers_callback = 0;
    body_callback = 0;
    if (close_tcp) net_tcp_close();
    if (callback) callback(success, context);
}

static bool parse_decimal(const char* value, usize length, u32* out) {
    if (!length) return false;
    u32 result = 0;
    for (usize i = 0; i < length; i++) {
        if (value[i] < '0' || value[i] > '9') return false;
        u32 digit = (u32)(value[i] - '0');
        if (result > (0xFFFFFFFFu - digit) / 10u) return false;
        result = result * 10u + digit;
    }
    *out = result;
    return true;
}

static bool parse_headers(void) {
    bool saw_transfer_encoding = false;
    header_buffer[header_length] = 0;
    char* text = (char*)header_buffer;
    char* line_end = kstrstr(text, "\r\n");
    if (!line_end || (usize)(line_end - text) < 12u ||
        kstrncmp(text, "HTTP/", 5u) != 0)
        return false;
    char* status_digits = kstrstr(text, " ");
    if (!status_digits || status_digits + 4 > line_end ||
        status_digits[1] < '0' || status_digits[1] > '9' ||
        status_digits[2] < '0' || status_digits[2] > '9' ||
        status_digits[3] < '0' || status_digits[3] > '9')
        return false;
    response_status = (u16)((status_digits[1] - '0') * 100 +
                            (status_digits[2] - '0') * 10 +
                            (status_digits[3] - '0'));

    char* line = line_end + 2;
    char* headers_end = kstrstr(line, "\r\n\r\n");
    if (!headers_end) return false;
    while (line < headers_end) {
        char* end = kstrstr(line, "\r\n");
        if (!end || end > headers_end) return false;
        char* colon = line;
        while (colon < end && *colon != ':') colon++;
        if (colon == line || colon == end) return false;
        char* value = colon + 1;
        while (value < end && (*value == ' ' || *value == '\t')) value++;
        usize name_length = (usize)(colon - line);
        usize value_length = (usize)(end - value);
        while (value_length && (value[value_length - 1u] == ' ' ||
                                value[value_length - 1u] == '\t'))
            value_length--;
        if (ascii_equal(line, name_length, "content-length")) {
            u32 parsed;
            if (!parse_decimal(value, value_length, &parsed)) return false;
            if (has_content_length && parsed != expected_body) return false;
            has_content_length = true;
            expected_body = parsed;
        } else if (ascii_equal(line, name_length, "transfer-encoding")) {
            if (saw_transfer_encoding ||
                !ascii_equal(value, value_length, "chunked"))
                return false;
            saw_transfer_encoding = true;
            chunked_encoding = true;
        }
        line = end + 2;
    }
    /* RFC framing is ambiguous when both mechanisms are present. */
    if (has_content_length && chunked_encoding) return false;
    headers_done = true;
    if (headers_callback)
        headers_callback(response_status, has_content_length, expected_body,
                         request_context);
    return true;
}

static void deliver_body(const u8* data, usize length) {
    if (!length || !active) return;
    if (has_content_length) {
        u32 remaining = expected_body > received_body
            ? expected_body - received_body : 0;
        if (length > remaining) length = remaining;
    }
    if (length && body_callback) body_callback(data, length, request_context);
    received_body += (u32)length;
    if (has_content_length && received_body >= expected_body)
        finish_request(true, true);
}

static bool parse_chunk_size(void) {
    usize n = chunk_line_length;
    if (n && chunk_line[n - 1u] == '\n') n--;
    if (n && chunk_line[n - 1u] == '\r') n--;
    if (!n) return false;
    u32 size = 0;
    usize digits = 0;
    for (usize i = 0; i < n && chunk_line[i] != ';'; i++) {
        char ch = chunk_line[i];
        u32 value;
        if (ch >= '0' && ch <= '9') value = (u32)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') value = (u32)(ch - 'a') + 10u;
        else if (ch >= 'A' && ch <= 'F') value = (u32)(ch - 'A') + 10u;
        else return false;
        if (size > (0xFFFFFFFFu - value) / 16u) return false;
        size = size * 16u + value;
        digits++;
    }
    if (!digits) return false;
    chunk_remaining = size;
    chunk_line_length = 0;
    if (size == 0) chunk_state = HTTP_CHUNK_TRAILER;
    else chunk_state = HTTP_CHUNK_DATA;
    return true;
}

static void consume_chunked(const u8* data, usize length) {
    usize pos = 0;
    while (pos < length && active && chunk_state != HTTP_CHUNK_DONE) {
        if (chunk_state == HTTP_CHUNK_DATA) {
            usize take = length - pos;
            if (take > chunk_remaining) take = chunk_remaining;
            if (take) deliver_body(data + pos, take);
            if (!active) return;
            pos += take;
            chunk_remaining -= (u32)take;
            if (chunk_remaining == 0) chunk_state = HTTP_CHUNK_DATA_CR;
        } else if (chunk_state == HTTP_CHUNK_DATA_CR) {
            if (data[pos++] != '\r') { finish_request(false, true); return; }
            chunk_state = HTTP_CHUNK_DATA_LF;
        } else if (chunk_state == HTTP_CHUNK_DATA_LF) {
            if (data[pos++] != '\n') { finish_request(false, true); return; }
            chunk_state = HTTP_CHUNK_SIZE;
        } else {
            char ch = (char)data[pos++];
            if (chunk_line_length + 1u >= sizeof(chunk_line)) {
                finish_request(false, true);
                return;
            }
            chunk_line[chunk_line_length++] = ch;
            if (ch == '\n') {
                usize n = chunk_line_length;
                if (n >= 2u && chunk_line[n - 2u] == '\r') n -= 2u;
                else { finish_request(false, true); return; }
                if (chunk_state == HTTP_CHUNK_SIZE) {
                    if (!parse_chunk_size()) { finish_request(false, true); return; }
                } else {
                    /* A blank trailer line terminates the chunked message. */
                    if (n == 0) {
                        chunk_state = HTTP_CHUNK_DONE;
                        finish_request(true, true);
                        return;
                    }
                    chunk_line_length = 0;
                }
            }
        }
    }
}

static void http_on_data(const u8* data, usize length, void* context) {
    (void)context;
    if (!active || !data) return;
    usize pos = 0;
    if (!headers_done) {
        while (pos < length && !headers_done) {
            if (header_length >= HTTP_HEADER_MAX) {
                finish_request(false, true);
                return;
            }
            header_buffer[header_length++] = data[pos++];
            if (header_length >= 4u &&
                header_buffer[header_length - 4u] == '\r' &&
                header_buffer[header_length - 3u] == '\n' &&
                header_buffer[header_length - 2u] == '\r' &&
                header_buffer[header_length - 1u] == '\n') {
                if (!parse_headers()) {
                    finish_request(false, true);
                    return;
                }
            }
        }
        if (headers_done &&
            ((response_status == 204u || response_status == 304u) ||
             (has_content_length && expected_body == 0))) {
            finish_request(true, true);
            return;
        }
    }
    if (headers_done && pos < length) {
        if (chunked_encoding) consume_chunked(data + pos, length - pos);
        else deliver_body(data + pos, length - pos);
    }
}

static void http_on_closed(bool clean, void* context) {
    (void)context;
    if (!active) return;
    bool complete = clean && headers_done &&
        (chunked_encoding ? chunk_state == HTTP_CHUNK_DONE :
         (!has_content_length || received_body >= expected_body));
    finish_request(complete, false);
}

static void http_on_connect(bool connected, void* context) {
    (void)context;
    if (!active) return;
    if (!connected) {
        finish_request(false, false);
        return;
    }
    usize offset = 0;
    request[0] = 0;
    if (!append_text(&offset, "GET ") ||
        !append_text(&offset, request_path) ||
        !append_text(&offset, " HTTP/1.0\r\nHost: ") ||
        !append_text(&offset, request_host) ||
        !append_text(&offset, "\r\nUser-Agent: CharisOS/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n")) {
        finish_request(false, true);
        return;
    }
    request_length = offset;
    int sent = net_tcp_send(request, request_length);
    if (sent < 0 && sent != -2) finish_request(false, true);
}

static void http_on_dns(const char* hostname, const u8 ip[4],
                        bool success, void* context) {
    (void)hostname;
    (void)context;
    if (!active) return;
    if (!success || !net_tcp_connect(ip, 80u, http_on_connect,
                                    http_on_data, http_on_closed, 0)) {
        finish_request(false, false);
    }
}

bool net_http_get(const char* hostname, const char* path,
                  net_http_headers_callback_t on_headers,
                  net_http_body_callback_t on_body,
                  net_http_complete_callback_t on_complete,
                  void* context) {
    if (active || !hostname || !path || path[0] != '/' || !on_complete)
        return false;
    usize host_len = kstrlen(hostname), path_len = kstrlen(path);
    if (!host_len || host_len > HTTP_HOST_MAX || !path_len ||
        path_len > HTTP_PATH_MAX)
        return false;
    for (usize i = 0; i < path_len; i++) {
        if ((u8)path[i] < 0x21u || path[i] == '\r' || path[i] == '\n')
            return false;
    }
    kstrncpy(request_host, hostname, sizeof(request_host));
    request_host[HTTP_HOST_MAX] = 0;
    kstrncpy(request_path, path, sizeof(request_path));
    request_path[HTTP_PATH_MAX] = 0;
    headers_callback = on_headers;
    body_callback = on_body;
    complete_callback = on_complete;
    request_context = context;
    header_length = 0;
    headers_done = false;
    has_content_length = false;
    chunked_encoding = false;
    chunk_state = HTTP_CHUNK_SIZE;
    chunk_line_length = 0;
    chunk_remaining = 0;
    response_status = 0;
    expected_body = 0;
    received_body = 0;
    request_length = 0;
    active = true;
    if (!net_dns_query(request_host, http_on_dns, 0)) {
        active = false;
        headers_callback = 0;
        body_callback = 0;
        complete_callback = 0;
        return false;
    }
    return true;
}

void net_http_cancel(void) {
    if (active) finish_request(false, true);
}
