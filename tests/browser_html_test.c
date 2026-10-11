/* Host-side tests for the bounded HTML-to-text preview parser. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/browser.c"

static void expect(bool ok, const char* message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void reset_html(void) {
    remote_text_length = 0;
    remote_text[0] = 0;
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
int main(void) {
    test_tags_and_block_boundaries();
    test_scripts_and_styles_are_hidden();
    test_entities_and_numeric_forms();
    test_entity_split_across_network_chunks();
    test_unknown_entities_remain_literal();
    test_navigation_reset_clears_partial_state();
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
