/* Host-side tests for the bounded HTML-to-text preview parser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/browser.c"

framebuffer_t g_framebuffer;

static void expect(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void reset_html(void) {
    remote_text_length = 0;
    remote_text[0] = 0;
    remote_title_length = 0;
    remote_title[0] = 0;
    remote_title_last_space = false;
    remote_scroll_lines = 0;
    remote_reset_parser();
}
static void feed(const char* text) {
    remote_body((const u8*)text, strlen(text), 0);
}
static void test_tags_and_block_boundaries(void) {
    reset_html();
    feed("<h1>Charis</h1><p>Operating   system</p>");
    expect(strcmp(remote_text, "Charis\nOperating system\n") == 0,
           "strip tags and preserve block boundaries");
}
static void test_scripts_and_styles_are_hidden(void) {
    reset_html();
    feed("<p>Visible</p><script type=\"text/javascript\">secret()</script><style media=\"screen\">.hidden{}</style><p>Still visible</p>");
    expect(strcmp(remote_text, "Visible\nStill visible\n") == 0,
           "script and style contents must not leak into text");
}
static void test_entities_and_numeric_forms(void) {
    reset_html();
    feed("A &amp; B &LT;tag&GT; &#39;quoted&#x21;");
    expect(strcmp(remote_text, "A & B <tag> 'quoted!") == 0,
           "named, case-insensitive and decimal/hex entities decode");
}
static void test_entity_split_across_network_chunks(void) {
    reset_html();
    feed("left &am");
    feed("p; right &#x");
    feed("41;");
    expect(strcmp(remote_text, "left & right A") == 0,
           "entity state must survive body callback boundaries");
}
static void test_html_comments_are_hidden_across_chunks(void) {
    reset_html();
    feed("before<!-- private > markup");
    feed(" and --");
    feed(">after");
    expect(strcmp(remote_text, "beforeafter") == 0,
           "HTML comments and embedded greater-than signs are hidden");
}
static void test_unknown_entities_remain_literal(void) {
    reset_html();
    feed("keep &notAnEntity; visible");
    expect(strcmp(remote_text, "keep &notAnEntity; visible") == 0,
           "unknown entities remain readable");
}
static void test_navigation_reset_clears_partial_state(void) {
    reset_html();
    feed("<script>hidden");
    expect(remote_suppress_content, "script enters suppression");
    remote_closing_tag = true;
    remote_in_entity = true;
    remote_entity_length = 2;
    remote_entity[0] = 'a';
    remote_entity[1] = 'm';
    remote_entity[2] = 0;
    remote_reset_parser();
    expect(!remote_suppress_content && !remote_in_entity &&
           !remote_closing_tag && remote_entity_length == 0,
           "new page starts with clean parser state");
    feed("new page");
    expect(strcmp(remote_text, "new page") == 0,
           "new page content is not hidden by stale parser state");
}
static void test_document_title_is_extracted_across_chunks(void) {
    reset_html();
    feed("<html><head><title>  Charis &amp;");
    feed(" OS  </title><style>.hidden{}</style></head><body><p>Visible body</p></body></html>");
    expect(strcmp(remote_title, "Charis & OS ") == 0 ||
           strcmp(remote_title, "Charis & OS") == 0,
           "document title survives callback boundaries and decodes entities");
    expect(strcmp(remote_text, "Visible body\n") == 0,
           "title text is metadata, not duplicated in body preview");
}
static void test_title_state_resets_for_new_page(void) {
    reset_html();
    feed("<title>Old title");
    expect(remote_in_title, "opening title enters title capture");
    remote_reset_parser();
    expect(!remote_in_title, "new page clears title capture state");
    feed("Body only");
    expect(strcmp(remote_text, "Body only") == 0,
           "content after navigation is not captured as stale title");
}
static void test_preview_keyboard_scroll_is_bounded(void) {
    reset_html();
    remote_ready = true;
    remote_loading = false;
    strcpy(remote_url, "http://example.com/");
    strcpy(tabs[0].address, "http://example.com/");
    tab_count = 1;
    active_tab = 0;
    g_framebuffer.initialized = true;
    g_framebuffer.height = 300;
    remote_text_length = 0;
    for (int i = 0; i < 30; i++) {
        remote_text[remote_text_length++] = 'x';
        remote_text[remote_text_length++] = '\n';
    }
    remote_text[remote_text_length] = 0;
    remote_scroll_lines = 0;
    browser_scroll_preview(false);
    expect(remote_scroll_lines == 0, "scroll up stays at top");
    browser_scroll_preview(true);
    expect(remote_scroll_lines == 5, "page down advances preview");
    for (int i = 0; i < 20; i++) browser_scroll_preview(true);
    expect(remote_scroll_lines <= preview_count_lines(remote_text, remote_text_length),
           "scroll offset remains bounded by document lines");
    for (int i = 0; i < 20; i++) browser_scroll_preview(false);
    expect(remote_scroll_lines == 0, "scroll up returns to top");
    remote_ready = false;
    remote_scroll_lines = 0;
    g_framebuffer.initialized = false;
}

static void test_preview_scroll_counts_soft_wrapped_rows(void) {
    char text[301];
    memset(text, 'x', 300);
    text[300] = 0;
    expect(preview_count_lines(text, 300) == 4,
           "300 unbroken characters occupy four visual rows");
    expect(preview_skip_lines(text, 300, 1) == 95,
           "scroll skips one rendered row, not the entire paragraph");
    expect(preview_skip_lines(text, 300, 3) == 285,
           "scroll offset follows the renderer's 95-character row width");
}

static void test_http_url_parser_normalizes_fragments_and_queries(void) {
    char host[128], path[128];
    expect(parse_http_url("http://example.com/path?q=1#section",
                          host, sizeof(host), path, sizeof(path)),
           "valid HTTP URL parses");
    expect(strcmp(host, "example.com") == 0, "hostname extracted");
    expect(strcmp(path, "/path?q=1") == 0,
           "fragment is removed from request target");
    expect(parse_http_url("http://example.com?search=hello#part",
                          host, sizeof(host), path, sizeof(path)),
           "query-only URL parses");
    expect(strcmp(path, "/?search=hello") == 0,
           "query-only URL receives a slash prefix");
    expect(parse_http_url("http://example.com:80/", host, sizeof(host),
                          path, sizeof(path)),
           "default HTTP port is accepted");
    expect(strcmp(host, "example.com") == 0, "default port is stripped");
}
static void test_http_url_parser_rejects_unsupported_or_malformed_urls(void) {
    char host[128], path[128];
    expect(!parse_http_url("https://example.com/", host, sizeof(host),
                           path, sizeof(path)), "HTTPS is not misrouted as HTTP");
    expect(!parse_http_url("http://bad host/", host, sizeof(host),
                           path, sizeof(path)), "hostname spaces rejected");
    expect(!parse_http_url("http://-bad.example/", host, sizeof(host),
                           path, sizeof(path)), "leading hyphen rejected");
    expect(!parse_http_url("http://example..com/", host, sizeof(host),
                           path, sizeof(path)), "empty hostname label rejected");
    expect(!parse_http_url("http://example.com:8080/", host, sizeof(host),
                           path, sizeof(path)), "unsupported port rejected");
    expect(!parse_http_url("http://example.com/a b", host, sizeof(host),
                           path, sizeof(path)), "path control and spaces rejected");
}

static void test_http_status_feedback_distinguishes_redirects_and_errors(void) {
    remote_loading = true;
    remote_status_code = 302u;
    remote_complete(true, 0);
    expect(strcmp(browser_status,
           "HTTP redirect received; automatic redirects are not supported yet") == 0,
           "redirect is reported without pretending it loaded");
    remote_loading = true;
    remote_status_code = 404u;
    remote_complete(true, 0);
    expect(strcmp(browser_status,
           "HTTP server returned an error status; response text may explain why") == 0,
           "server error status is visible to the user");
    remote_loading = true;
    remote_status_code = 200u;
    remote_complete(true, 0);
    expect(strcmp(browser_status, "HTTP text preview loaded") == 0,
           "successful HTTP status is reported");
}
int main(void) {
    test_tags_and_block_boundaries();
    test_scripts_and_styles_are_hidden();
    test_entities_and_numeric_forms();
    test_entity_split_across_network_chunks();
    test_html_comments_are_hidden_across_chunks();
    test_unknown_entities_remain_literal();
    test_navigation_reset_clears_partial_state();
    test_document_title_is_extracted_across_chunks();
    test_title_state_resets_for_new_page();
    test_preview_keyboard_scroll_is_bounded();
    test_preview_scroll_counts_soft_wrapped_rows();
    test_http_url_parser_normalizes_fragments_and_queries();
    test_http_url_parser_rejects_unsupported_or_malformed_urls();
    test_http_status_feedback_distinguishes_redirects_and_errors();
    puts("Browser HTML extraction tests passed");
    return 0;
}

/* Minimal kernel utility stubs for host-side parser tests. */
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
void kstrcat(char* d, const char* s) { strcat(d, s); }
