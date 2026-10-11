/* http.h - Minimal asynchronous HTTP/1.x client over DNS and TCP. */
#pragma once
#include <kernel/types.h>

typedef void (*net_http_headers_callback_t)(u16 status_code,
                                            bool has_content_length,
                                            u32 content_length,
                                            void* context);
typedef void (*net_http_body_callback_t)(const u8* data, usize length,
                                         void* context);
typedef void (*net_http_complete_callback_t)(bool success, void* context);

/* Fetches plain HTTP on port 80. One request at a time; HTTPS/TLS is not
 * implemented. Body chunks are borrowed and valid only during the callback. */
bool net_http_get(const char* hostname, const char* path,
                  net_http_headers_callback_t on_headers,
                  net_http_body_callback_t on_body,
                  net_http_complete_callback_t on_complete,
                  void* context);
void net_http_cancel(void);
