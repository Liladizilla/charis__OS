/* browser.c - Lightweight browser shell and local Charis pages.
 *
 * This provides browser chrome, tabs, address entry, local navigation,
 * history, reload and bookmarks. Remote HTTP/HTTPS rendering is deliberately
 * reported as unavailable until the kernel has a real TCP/IP + DNS + TLS/HTTP
 * stack and a document renderer. A toolbar is not a networking stack.
 */
#include <kernel/browser.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/keyboard.h>
#include <kernel/string.h>
#include <kernel/ui_theme.h>
#include <kernel/http.h>
#include <kernel/vfs.h>
#include <kernel/memory.h>

#define BROWSER_MAX_TABS 4
#define BROWSER_HISTORY  8
#define BROWSER_URL_MAX  128
#define BROWSER_TASKBAR  44

typedef struct {
    char address[BROWSER_URL_MAX];
    char history[BROWSER_HISTORY][BROWSER_URL_MAX];
    int history_count;
    int history_index;
    bool bookmarked;
    bool address_focus;
    bool select_all;
} browser_tab_t;

static browser_tab_t tabs[BROWSER_MAX_TABS];
static int tab_count;
static int active_tab;
static bool browser_visible;
static bool browser_shift;
static bool browser_ctrl;
static char browser_status[96] = "Ready";
static char browser_bookmark[BROWSER_URL_MAX];
static bool browser_has_bookmark;

/* Remote pages are currently displayed as a bounded plain-text preview.
 * This is not yet an HTML/CSS document engine and intentionally has no script execution. */
static char remote_url[BROWSER_URL_MAX];
static char remote_text[2048];
static usize remote_text_length;
static u16 remote_status_code;
static bool remote_loading;
static bool remote_ready;
static bool remote_in_tag;
static bool remote_closing_tag;
static bool remote_tag_name_done;
static bool remote_skip_content;
static char remote_tag_name[8];
static usize remote_tag_length;
static bool remote_last_space;
static char file_preview_url[BROWSER_URL_MAX];
static char file_preview[2048];
static usize file_preview_length;
static bool file_preview_ready;
static const u32 C_BG = CHARIS_UI_BACKGROUND;
static const u32 C_PANEL = CHARIS_UI_PANEL;
static const u32 C_RAISED = CHARIS_UI_PANEL_RAISED;
static const u32 C_SURFACE = CHARIS_UI_SURFACE;
static const u32 C_BORDER = CHARIS_UI_BORDER;
static const u32 C_TEXT = CHARIS_UI_TEXT;
static const u32 C_MUTED = CHARIS_UI_MUTED;
static const u32 C_ACCENT = CHARIS_UI_ACCENT;
static const u32 C_ACCENT_BG = CHARIS_UI_ACCENT_SURFACE;
static const u32 C_WARNING = CHARIS_UI_WARNING;

static browser_tab_t* current_tab(void) {
    if (tab_count <= 0) return NULL;
    if (active_tab < 0 || active_tab >= tab_count) active_tab = 0;
    return &tabs[active_tab];
}

static void set_status(const char* text) {
    kstrncpy(browser_status, text, sizeof(browser_status) - 1);
    browser_status[sizeof(browser_status) - 1] = 0;
}

static void address_set(browser_tab_t* tab, const char* address) {
    if (!tab || !address) return;
    kstrncpy(tab->address, address, sizeof(tab->address) - 1);
    tab->address[sizeof(tab->address) - 1] = 0;
}

static void history_push(browser_tab_t* tab, const char* address) {
    if (!tab || !address) return;
    if (tab->history_count > 0 &&
        kstrcmp(tab->history[tab->history_index], address) == 0) return;
    tab->history_index++;
    if (tab->history_index >= BROWSER_HISTORY) {
        for (int i = 1; i < BROWSER_HISTORY; i++)
            kstrncpy(tab->history[i - 1], tab->history[i], BROWSER_URL_MAX - 1);
        tab->history_index = BROWSER_HISTORY - 1;
        tab->history_count = BROWSER_HISTORY - 1;
    }
    address_set((browser_tab_t*)tab, address);
    kstrncpy(tab->history[tab->history_index], address, BROWSER_URL_MAX - 1);
    tab->history[tab->history_index][BROWSER_URL_MAX - 1] = 0;
    tab->history_count = tab->history_index + 1;
}

static void remote_append(char ch) {
    if (remote_text_length + 1u >= sizeof(remote_text)) return;
    remote_text[remote_text_length++] = ch;
    remote_text[remote_text_length] = 0;
}

static bool remote_block_tag(void) {
    return kstrcmp(remote_tag_name, "p") == 0 ||
           kstrcmp(remote_tag_name, "br") == 0 ||
           kstrcmp(remote_tag_name, "div") == 0 ||
           kstrcmp(remote_tag_name, "li") == 0 ||
           kstrcmp(remote_tag_name, "h1") == 0 ||
           kstrcmp(remote_tag_name, "h2") == 0 ||
           kstrcmp(remote_tag_name, "h3") == 0 ||
           kstrcmp(remote_tag_name, "h4") == 0 ||
           kstrcmp(remote_tag_name, "tr") == 0 ||
           kstrcmp(remote_tag_name, "hr") == 0;
}

static void remote_headers(u16 status, bool has_length, u32 length, void* context) {
    (void)has_length;
    (void)length;
    (void)context;
    remote_status_code = status;
}

static void remote_body(const u8* data, usize length, void* context) {
    (void)context;
    if (!data) return;
    for (usize i = 0; i < length; i++) {
        char ch = (char)data[i];
        if (remote_in_tag) {
            if (ch == '>') {
                remote_in_tag = false;
                bool hidden_tag = kstrcmp(remote_tag_name, "head") == 0 ||
                                  kstrcmp(remote_tag_name, "script") == 0 ||
                                  kstrcmp(remote_tag_name, "style") == 0;
                if (hidden_tag) remote_skip_content = !remote_closing_tag;
                if (!remote_skip_content && remote_block_tag() && remote_text_length &&
                    remote_text[remote_text_length - 1u] != '\n')
                    remote_append('\n');
                remote_tag_length = 0;
                remote_tag_name[0] = 0;
                remote_tag_name_done = false;
                remote_closing_tag = false;
            } else if (!remote_tag_name_done) {
                if (ch == '/' && remote_tag_length == 0) {
                    remote_closing_tag = true;
                } else if ((ch >= 'A' && ch <= 'Z') ||
                           (ch >= 'a' && ch <= 'z') ||
                           (ch >= '0' && ch <= '9')) {
                    if (remote_tag_length + 1u < sizeof(remote_tag_name)) {
                        char lower = ch >= 'A' && ch <= 'Z' ? (char)(ch - 'A' + 'a') : ch;
                        remote_tag_name[remote_tag_length++] = lower;
                        remote_tag_name[remote_tag_length] = 0;
                    }
                } else {
                    remote_tag_name_done = true;
                }
            }
            continue;
        }
        if (ch == '<') {
            remote_in_tag = true;
            remote_tag_length = 0;
            remote_tag_name[0] = 0;
            remote_tag_name_done = false;
            remote_closing_tag = false;
            continue;
        }
        if (remote_skip_content) continue;
        if (ch == '&') {
            /* Entity decoding is intentionally conservative in this first renderer. */
            remote_append(ch);
            remote_last_space = false;
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
            if (remote_text_length && !remote_last_space &&
                remote_text[remote_text_length - 1u] != '\n') {
                remote_append(' ');
                remote_last_space = true;
            }
        } else if ((u8)ch >= 0x20u) {
            remote_append(ch);
            remote_last_space = false;
        }
    }
}

static void remote_complete(bool success, void* context) {
    (void)context;
    remote_loading = false;
    remote_ready = true;
    if (success) {
        set_status(remote_status_code ? "HTTP text preview loaded" : "HTTP response received");
    } else {
        set_status("HTTP request failed; check network configuration");
    }
}

static bool valid_http_hostname(const char* host, usize length) {
    if (!host || !length || length > 253u) return false;
    usize label_length = 0;
    for (usize i = 0; i < length; i++) {
        char ch = host[i];
        if (ch == '.') {
            if (!label_length || label_length > 63u) return false;
            label_length = 0;
            continue;
        }
        bool alpha = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
        bool digit = ch >= '0' && ch <= '9';
        if (!alpha && !digit && ch != '-') return false;
        if (!label_length && ch == '-') return false;
        label_length++;
        if (label_length > 63u) return false;
    }
    if (!label_length || host[length - 1u] == '-') return false;
    return true;
}

static bool valid_http_path(const char* path) {
    if (!path || path[0] != '/') return false;
    for (usize i = 0; path[i]; i++) {
        u8 ch = (u8)path[i];
        if (ch <= 0x20u || ch == 0x7Fu) return false;
    }
    return true;
}

static bool remote_navigate(const char* url) {
    if (!url || kstrncmp(url, "http://", 7u) != 0) return false;
    const char* host = url + 7u;
    const char* path = kstrchr(host, '/');
    const char* query = kstrchr(host, '?');
    const char* url_fragment = kstrchr(host, (char)35);
    const char* end = path;
    if (!end || (query && query < end)) end = query;
    if (url_fragment && (!end || url_fragment < end)) end = url_fragment;
    if (!end) end = host + kstrlen(host);
    usize host_len = (usize)(end - host);
    if (!host_len || host_len >= sizeof(remote_url)) return false;
    /* Reject user-info, whitespace, IPv6 literals and malformed host labels. */
    if (!valid_http_hostname(host, host_len)) return false;

    char hostname[BROWSER_URL_MAX];
    char path_buffer[BROWSER_URL_MAX];
    if (host_len >= sizeof(hostname)) return false;
    kstrncpy(hostname, host, host_len);
    hostname[host_len] = 0;
    if (kstrchr(hostname, ':')) {
        /* Only the default HTTP port is supported by this first client. */
        char* colon = kstrchr(hostname, ':');
        if (kstrcmp(colon, ":80") != 0) return false;
        *colon = 0;
        if (!hostname[0]) return false;
    }
    if (end[0] == '/') {
        kstrncpy(path_buffer, end, sizeof(path_buffer) - 1u);
        path_buffer[sizeof(path_buffer) - 1u] = 0;
    } else if (end[0] == '?') {
        path_buffer[0] = '/';
        kstrncpy(path_buffer + 1u, end, sizeof(path_buffer) - 2u);
        path_buffer[sizeof(path_buffer) - 1u] = 0;
    } else {
        kstrncpy(path_buffer, "/", sizeof(path_buffer));
    }
    /* URL fragments are client-side only and must never be sent in HTTP. */
    char* fragment = kstrchr(path_buffer, '#');
    if (fragment) *fragment = 0;
    if (!valid_http_path(path_buffer)) return false;

    if (remote_loading) net_http_cancel();
    kstrncpy(remote_url, url, sizeof(remote_url) - 1u);
    remote_url[sizeof(remote_url) - 1u] = 0;
    remote_text_length = 0;
    remote_text[0] = 0;
    remote_status_code = 0;
    remote_in_tag = false;
    remote_closing_tag = false;
    remote_tag_name_done = false;
    remote_skip_content = false;
    remote_tag_length = 0;
    remote_tag_name[0] = 0;
    remote_last_space = false;
    remote_ready = false;
    remote_loading = true;
    set_status("Resolving host over plain HTTP (no TLS)...");
    if (!net_http_get(hostname, path_buffer, remote_headers, remote_body,
                      remote_complete, 0)) {
        remote_loading = false;
        remote_ready = true;
        set_status("HTTP unavailable; check network and static IP settings");
        return false;
    }
    return true;
}

static bool file_path_has_parent(const char* path) {
    usize i = 0;
    while (path && path[i]) {
        while (path[i] == '/') i++;
        usize start = i;
        while (path[i] && path[i] != '/') i++;
        if (i - start == 2u && path[start] == '.' && path[start + 1u] == '.')
            return true;
    }
    return false;
}

static bool load_local_file(const char* url) {
    file_preview_ready = false;
    file_preview_length = 0;
    file_preview[0] = 0;
    if (!url || kstrncmp(url, "file://", 7u) != 0) return false;
    const char* path = url + 7u;
    usize path_len = kstrlen(path);
    if (!path_len || path[0] != '/' || path_len >= BROWSER_URL_MAX ||
        file_path_has_parent(path) || kstrchr(path, '?') || kstrchr(path, '#') ||
        kstrchr(path, '\\'))
        return false;

    vfs_node_t* node = vfs_resolve(path);
    if (!node || node->flags != VFS_FILE || !node->read) {
        if (node && node->flags == VFS_FILE) kfree(node);
        return false;
    }
    usize limit = node->size < sizeof(file_preview) - 1u
        ? (usize)node->size : sizeof(file_preview) - 1u;
    int bytes = node->read(node, 0, limit, (u8*)file_preview);
    kfree(node);
    if (bytes < 0) return false;
    usize count = (usize)bytes;
    if (count > limit) count = limit;
    usize output = 0;
    for (usize i = 0; i < count; i++) {
        u8 ch = (u8)file_preview[i];
        if (ch == 0) return false; /* refuse binary data */
        if (ch == '\r') continue;
        if (ch == '\t') ch = ' ';
        if (ch == '\n' || (ch >= 0x20u && ch != 0x7Fu))
            file_preview[output++] = (char)ch;
    }
    file_preview[output] = 0;
    file_preview_length = output;
    kstrncpy(file_preview_url, url, sizeof(file_preview_url) - 1u);
    file_preview_url[sizeof(file_preview_url) - 1u] = 0;
    file_preview_ready = true;
    return true;
}

static void navigate(const char* raw, bool add_history) {
    browser_tab_t* tab = current_tab();
    if (!tab || !raw || !raw[0]) return;

    char target[BROWSER_URL_MAX];
    if (kstrncmp(raw, "charis://", 9) == 0 ||
        kstrncmp(raw, "http://", 7) == 0 ||
        kstrncmp(raw, "https://", 8) == 0 ||
        kstrncmp(raw, "file://", 7) == 0) {
        kstrncpy(target, raw, sizeof(target) - 1);
        target[sizeof(target) - 1] = 0;
    } else {
        /* Until TLS exists, a bare hostname must use the supported HTTP path. */
        /* This is intentionally explicit in behavior, not a silent HTTPS retry. */
        target[0] = 'h'; target[1] = 't'; target[2] = 't'; target[3] = 'p';
        target[4] = ':'; target[5] = '/'; target[6] = '/';
        kstrncpy(target + 7, raw, sizeof(target) - 8);
        target[sizeof(target) - 1] = 0;
    }

    if (remote_loading && kstrcmp(target, remote_url) != 0)
        net_http_cancel();
    address_set(tab, target);
    tab->address_focus = false;
    tab->select_all = false;
    if (add_history) history_push(tab, target);

    if (kstrcmp(target, "charis://home") == 0 ||
        kstrcmp(target, "charis://") == 0) {
        set_status("Local start page");
    } else if (kstrcmp(target, "charis://about") == 0) {
        set_status("CharisOS browser information");
    } else if (kstrcmp(target, "charis://help") == 0) {
        set_status("Browser help");
    } else if (kstrcmp(target, "charis://bookmarks") == 0) {
        set_status("Saved local bookmark list");
    } else if (kstrncmp(target, "http://", 7) == 0) {
        if (!remote_navigate(target))
            set_status("HTTP preview unavailable; only default port 80 is supported");
    } else if (kstrncmp(target, "https://", 8) == 0) {
        set_status("HTTPS unavailable until TLS and certificate validation exist");
    } else if (kstrncmp(target, "file://", 7) == 0) {
        if (load_local_file(target))
            set_status("Local file preview loaded");
        else
            set_status("Cannot open file; use a text file under the VFS root");
    } else {
        set_status("Unknown local page. Try charis://home");
    }
}

void browser_open(void) {
    browser_visible = true;
    if (tab_count == 0) {
        tab_count = 1;
        active_tab = 0;
        kmemset(&tabs[0], 0, sizeof(tabs[0]));
        tabs[0].history_index = -1;
        address_set(&tabs[0], "charis://home");
        history_push(&tabs[0], "charis://home");
    }
    set_status("Local start page");
}

void browser_close(void) {
    if (remote_loading) net_http_cancel();
    browser_visible = false;
}

bool browser_is_open(void) {
    return browser_visible;
}

static void draw_button(u32 x, u32 y, u32 w, u32 h, const char* label, bool active) {
    fb_fill_rect(x, y, w, h, active ? C_ACCENT_BG : C_RAISED);
    fb_draw_rect(x, y, w, h, C_BORDER);
    gfx_draw_string(x + 8, y + (h - 16) / 2, label, active ? C_ACCENT : C_TEXT, FB_TRANSPARENT);
}

static void draw_card(u32 x, u32 y, u32 w, const char* eyebrow, const char* title, const char* detail) {
    fb_fill_rect(x + 3, y + 3, w, 88, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, 88, C_PANEL);
    fb_draw_rect(x, y, w, 88, C_BORDER);
    gfx_draw_string(x + 14, y + 10, eyebrow, C_ACCENT, FB_TRANSPARENT);
    gfx_draw_string(x + 14, y + 32, title, C_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + 14, y + 56, detail, C_MUTED, FB_TRANSPARENT);
}

static void draw_page(browser_tab_t* tab, u32 x, u32 y, u32 w, u32 h) {
    fb_fill_rect(x, y, w, h, C_BG);
    fb_draw_rect(x, y, w, h, C_BORDER);
    const char* address = tab ? tab->address : "charis://home";

    if (kstrcmp(address, "charis://home") == 0 || kstrcmp(address, "charis://") == 0) {
        gfx_draw_string(x + 24, y + 22, "CHARIS BROWSER", C_ACCENT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 54, "A local-first browser shell for CharisOS.", C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 80, "Local pages work now. Internet transport is still under development.", C_MUTED, FB_TRANSPARENT);
        u32 card_w = (w > 760) ? (w - 72) / 3 : (w - 48) / 2;
        if (card_w > 300) card_w = 300;
        draw_card(x + 24, y + 126, card_w, "START HERE", "About CharisOS", "System and browser details");
        draw_card(x + 36 + card_w, y + 126, card_w, "QUICK GUIDE", "Browser help", "Navigation and keyboard controls");
        if (w > card_w * 2 + 120)
            draw_card(x + 48 + card_w * 2, y + 126, card_w, "PERSONAL", "Bookmarks", "Your saved local page");
        gfx_draw_string(x + 24, y + h - 32, "Tip: Ctrl+L focuses the address bar. Enter opens a local page or attempts a web URL.", C_MUTED, FB_TRANSPARENT);
    } else if (kstrcmp(address, "charis://about") == 0) {
        gfx_draw_string(x + 24, y + 22, "ABOUT CHARIS BROWSER", C_ACCENT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 62, "Built into CharisOS. No external browser runtime is required for local pages.", C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 92, "This version includes tabs, address entry, local navigation, history and bookmarks.", C_MUTED, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 122, "Remote pages use a basic HTTP text preview; HTTPS and full HTML/CSS are still unsupported.", C_WARNING, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 152, "Networking supports static IPv4, DNS, TCP and plain HTTP, with limited protocol coverage.", C_MUTED, FB_TRANSPARENT);
    } else if (kstrcmp(address, "charis://help") == 0) {
        gfx_draw_string(x + 24, y + 22, "BROWSER HELP", C_ACCENT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 58, "Click the address bar or press Ctrl+L, type a URL, then press Enter.", C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 86, "Use Back, Forward, Reload and Home to move through local page history.", C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 114, "Click the star to bookmark a page. Open Bookmarks from the start page.", C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 154, "Escape leaves address editing; Escape again closes the browser.", C_MUTED, FB_TRANSPARENT);
    } else if (kstrcmp(address, "charis://bookmarks") == 0) {
        gfx_draw_string(x + 24, y + 22, "BOOKMARKS", C_ACCENT, FB_TRANSPARENT);
        if (browser_has_bookmark) {
            gfx_draw_string(x + 24, y + 62, browser_bookmark, C_TEXT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 90, "Click the saved entry to open it again.", C_MUTED, FB_TRANSPARENT);
        } else {
            gfx_draw_string(x + 24, y + 62, "No bookmarks saved yet.", C_TEXT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 90, "Open a page and click the star to save it.", C_MUTED, FB_TRANSPARENT);
        }
    } else if (kstrncmp(address, "http://", 7) == 0) {
        if (remote_ready && kstrcmp(address, remote_url) == 0) {
            gfx_draw_string(x + 24, y + 18, "HTTP TEXT PREVIEW", C_ACCENT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 42, address, C_MUTED, FB_TRANSPARENT);
            char status_line[48];
            kstrncpy(status_line, "HTTP status: ", sizeof(status_line) - 1u);
            status_line[sizeof(status_line) - 1u] = 0;
            if (remote_status_code >= 100 && remote_status_code <= 599) {
                usize n = kstrlen(status_line);
                status_line[n++] = (char)('0' + remote_status_code / 100u);
                status_line[n++] = (char)('0' + (remote_status_code / 10u) % 10u);
                status_line[n++] = (char)('0' + remote_status_code % 10u);
                status_line[n] = 0;
            } else {
                kstrncpy(status_line, "HTTP response", sizeof(status_line) - 1u);
            }
            gfx_draw_string(x + 24, y + 64, status_line, C_TEXT, FB_TRANSPARENT);
            u32 line_y = y + 94;
            usize pos = 0;
            while (pos < remote_text_length && line_y + 18u < y + h) {
                char line[96];
                usize n = 0;
                while (pos < remote_text_length && n < sizeof(line) - 1u &&
                       remote_text[pos] != '\n') {
                    line[n++] = remote_text[pos++];
                }
                if (pos < remote_text_length && remote_text[pos] == '\n') pos++;
                line[n] = 0;
                if (n) gfx_draw_string(x + 24, line_y, line, C_TEXT, FB_TRANSPARENT);
                line_y += 20u;
            }
            if (!remote_text_length)
                gfx_draw_string(x + 24, line_y, "The server returned no displayable text.", C_MUTED, FB_TRANSPARENT);
        } else {
            gfx_draw_string(x + 24, y + 22, remote_loading ? "LOADING HTTP PAGE..." : "HTTP PAGE NOT LOADED", C_ACCENT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 60, address, C_TEXT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 104, "CharisOS is fetching plain HTTP and showing a text-only preview.", C_TEXT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 134, "HTTPS, JavaScript and full HTML/CSS layout are not supported yet.", C_MUTED, FB_TRANSPARENT);
        }
    } else if (kstrncmp(address, "https://", 8) == 0) {
        gfx_draw_string(x + 24, y + 22, "HTTPS NOT SUPPORTED YET", C_WARNING, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 60, address, C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 104, "TLS and certificate validation must be implemented before HTTPS.", C_MUTED, FB_TRANSPARENT);
    } else {
        if (file_preview_ready && kstrcmp(address, file_preview_url) == 0) {
            gfx_draw_string(x + 24, y + 22, "LOCAL TEXT FILE", C_ACCENT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 48, address, C_MUTED, FB_TRANSPARENT);
            u32 line_y = y + 78;
            usize pos = 0;
            while (pos < file_preview_length && line_y + 18u < y + h) {
                char line[96];
                usize n = 0;
                while (pos < file_preview_length && n < sizeof(line) - 1u &&
                       file_preview[pos] != '\n') {
                    line[n++] = file_preview[pos++];
                }
                if (pos < file_preview_length && file_preview[pos] == '\n') pos++;
                line[n] = 0;
                if (n) gfx_draw_string(x + 24, line_y, line, C_TEXT, FB_TRANSPARENT);
                line_y += 20u;
            }
            if (!file_preview_length)
                gfx_draw_string(x + 24, line_y, "This file is empty.", C_MUTED, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + h - 28, "Read-only preview, limited to 2 KiB.", C_MUTED, FB_TRANSPARENT);
        } else {
            gfx_draw_string(x + 24, y + 22, "LOCAL FILE UNAVAILABLE", C_WARNING, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 60, address, C_TEXT, FB_TRANSPARENT);
            gfx_draw_string(x + 24, y + 98, "Use file:///path/to/text-file. Parent paths and binary files are rejected.", C_MUTED, FB_TRANSPARENT);
        }
    }
}

void browser_render(void) {
    if (!browser_visible || !g_framebuffer.initialized) return;
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    if (sw < 320 || sh < 220) return;
    u32 x = 12, y = 10, w = sw - 24;
    u32 h = sh > BROWSER_TASKBAR + 22 ? sh - BROWSER_TASKBAR - 18 : sh - 18;
    browser_tab_t* tab = current_tab();
    if (!tab) return;

    fb_fill_rect(x + 5, y + 5, w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, C_PANEL);
    fb_draw_rect(x, y, w, h, C_BORDER);

    /* Tab strip, including a visible close control and a new-tab control. */
    fb_fill_rect(x + 1, y + 1, w - 2, 34, C_BG);
    u32 tab_x = x + 12;
    for (int i = 0; i < tab_count; i++) {
        u32 tw = 156;
        bool selected = i == active_tab;
        fb_fill_rect(tab_x, y + 5, tw, 28, selected ? C_PANEL : C_SURFACE);
        fb_draw_rect(tab_x, y + 5, tw, 28, C_BORDER);
        gfx_draw_string(tab_x + 10, y + 13, "B", C_ACCENT, FB_TRANSPARENT);
        gfx_draw_string(tab_x + 30, y + 13, "Charis Browser", selected ? C_TEXT : C_MUTED, FB_TRANSPARENT);
        tab_x += tw + 6;
    }
    draw_button(tab_x, y + 5, 30, 28, "+", false);
    draw_button(x + w - 34, y + 5, 26, 28, "x", false);

    /* Navigation controls and address bar. */
    u32 nav_y = y + 42;
    draw_button(x + 10, nav_y, 34, 30, "<", tab->history_index > 0);
    draw_button(x + 50, nav_y, 34, 30, ">", tab->history_index + 1 < tab->history_count);
    draw_button(x + 90, nav_y, 34, 30, "R", true);
    draw_button(x + 130, nav_y, 34, 30, "H", true);
    u32 addr_x = x + 174;
    u32 star_x = x + w - 86;
    u32 addr_w = star_x - addr_x - 8;
    fb_fill_rect(addr_x, nav_y, addr_w, 30, tab->address_focus ? C_ACCENT_BG : C_SURFACE);
    fb_draw_rect(addr_x, nav_y, addr_w, 30, tab->address_focus ? C_ACCENT : C_BORDER);
    char address_display[BROWSER_URL_MAX];
    const char* shown = tab->address;
    usize max_chars = addr_w > 16 ? (addr_w - 16) / 8 : 0;
    if (tab->select_all) {
        kstrncpy(address_display, "Type address...", sizeof(address_display) - 1);
        address_display[sizeof(address_display) - 1] = 0;
        shown = address_display;
    } else if (kstrlen(tab->address) > max_chars && max_chars > 4) {
        usize len = kstrlen(tab->address);
        usize keep = max_chars - 3;
        address_display[0] = '.';
        address_display[1] = '.';
        address_display[2] = '.';
        kstrncpy(address_display + 3, tab->address + len - keep, keep);
        address_display[max_chars] = 0;
        shown = address_display;
    }
    gfx_draw_string(addr_x + 8, nav_y + 8, shown, tab->select_all ? C_MUTED : C_TEXT, FB_TRANSPARENT);
    tab->bookmarked = browser_has_bookmark && kstrcmp(tab->address, browser_bookmark) == 0;
    draw_button(star_x, nav_y, 72, 30, tab->bookmarked ? "* Saved" : "* Save", tab->bookmarked);

    /* Main page area and status bar. */
    u32 page_y = nav_y + 40;
    u32 status_h = 24;
    u32 page_h = h > page_y - y + status_h + 20 ? h - (page_y - y) - status_h - 12 : 80;
    draw_page(tab, x + 10, page_y, w - 20, page_h);
    fb_fill_rect(x + 1, y + h - status_h - 1, w - 2, status_h, C_BG);
    gfx_draw_string(x + 12, y + h - status_h + 4, browser_status, C_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + w - 210, y + h - status_h + 4, "LOCAL-FIRST  |  CHARISOS", C_ACCENT, FB_TRANSPARENT);
}

static void new_tab(void) {
    if (tab_count >= BROWSER_MAX_TABS) {
        set_status("Tab limit reached");
        return;
    }
    active_tab = tab_count++;
    kmemset(&tabs[active_tab], 0, sizeof(tabs[active_tab]));
    tabs[active_tab].history_index = -1;
    address_set(&tabs[active_tab], "charis://home");
    history_push(&tabs[active_tab], "charis://home");
    set_status("New local tab");
}

static void close_tab(void) {
    if (tab_count <= 1) {
        browser_close();
        return;
    }
    for (int i = active_tab; i < tab_count - 1; i++) tabs[i] = tabs[i + 1];
    tab_count--;
    if (active_tab >= tab_count) active_tab = tab_count - 1;
    set_status("Tab closed");
}

bool browser_handle_mouse(int mx, int my) {
    if (!browser_visible || !g_framebuffer.initialized) return false;
    u32 sw = g_framebuffer.width, sh = g_framebuffer.height;
    u32 x = 12, y = 10, w = sw - 24;
    u32 h = sh > BROWSER_TASKBAR + 22 ? sh - BROWSER_TASKBAR - 18 : sh - 18;
    if (mx < (int)x || mx >= (int)(x + w) || my < (int)y || my >= (int)(y + h)) {
        browser_close();
        return true;
    }
    browser_tab_t* tab = current_tab();
    if (!tab) return true;

    if (my >= (int)(y + 5) && my < (int)(y + 33)) {
        u32 tab_x = x + 12;
        for (int i = 0; i < tab_count; i++) {
            if (mx >= (int)tab_x && mx < (int)(tab_x + 156)) {
                active_tab = i;
                return true;
            }
            tab_x += 162;
        }
        if (mx >= (int)tab_x && mx < (int)(tab_x + 30)) new_tab();
        else if (mx >= (int)(x + w - 34)) close_tab();
        return true;
    }

    u32 nav_y = y + 42;
    if (my >= (int)nav_y && my < (int)(nav_y + 30)) {
        if (mx >= (int)(x + 10) && mx < (int)(x + 44)) {
            if (tab->history_index > 0) {
                tab->history_index--;
                address_set(tab, tab->history[tab->history_index]);
                navigate(tab->address, false);
                set_status("Back");
            }
        } else if (mx >= (int)(x + 50) && mx < (int)(x + 84)) {
            if (tab->history_index + 1 < tab->history_count) {
                tab->history_index++;
                address_set(tab, tab->history[tab->history_index]);
                navigate(tab->address, false);
                set_status("Forward");
            }
        } else if (mx >= (int)(x + 90) && mx < (int)(x + 124)) {
            navigate(tab->address, false);
            set_status("Reloaded local page");
        } else if (mx >= (int)(x + 130) && mx < (int)(x + 164)) {
            navigate("charis://home", true);
        } else if (mx >= (int)(x + w - 86)) {
            if (browser_has_bookmark && kstrcmp(browser_bookmark, tab->address) == 0) {
                browser_has_bookmark = false;
                browser_bookmark[0] = 0;
                tab->bookmarked = false;
                set_status("Bookmark removed");
            } else {
                address_set((browser_tab_t*)tab, tab->address);
                kstrncpy(browser_bookmark, tab->address, sizeof(browser_bookmark) - 1);
                browser_bookmark[sizeof(browser_bookmark) - 1] = 0;
                browser_has_bookmark = true;
                tab->bookmarked = true;
                set_status("Bookmark saved");
            }
        } else if (mx >= (int)(x + 174)) {
            tab->address_focus = true;
            tab->select_all = true;
            set_status("Address selected");
        }
        return true;
    }

    u32 page_y = nav_y + 40;
    if (kstrcmp(tab->address, "charis://bookmarks") == 0 && browser_has_bookmark &&
        my >= (int)(page_y + 48) && my < (int)(page_y + 112)) {
        navigate(browser_bookmark, true);
    } else if (my >= (int)(page_y + 120)) {
        u32 rel_y = (u32)my - page_y;
        if (kstrcmp(tab->address, "charis://home") == 0 &&
            rel_y >= 126 && rel_y < 220) {
            u32 card_w = (w > 760) ? (w - 72) / 3 : (w - 48) / 2;
            if (card_w > 300) card_w = 300;
            if (mx < (int)(x + 24 + card_w + 12)) navigate("charis://about", true);
            else if (mx < (int)(x + 36 + card_w * 2 + 12)) navigate("charis://help", true);
            else navigate("charis://bookmarks", true);
        }
    }
    return true;
}

bool browser_handle_key(u8 scancode, bool pressed) {
    if (!browser_visible) return false;
    u8 code = scancode & 0x7F;
    if (code == 0x2A || code == 0x36) { browser_shift = pressed; return true; }
    if (code == 0x1D) { browser_ctrl = pressed; return true; }
    if (!pressed) return true;
    if (code == KEY_ESCAPE) {
        browser_tab_t* tab = current_tab();
        if (tab && tab->address_focus) {
            tab->address_focus = false;
            tab->select_all = false;
            set_status("Address editing cancelled");
        } else browser_close();
        return true;
    }
    browser_tab_t* tab = current_tab();
    if (!tab) return true;
    if (browser_ctrl && code == 0x26) {
        tab->address_focus = true;
        tab->select_all = true;
        set_status("Address selected");
        return true;
    }
    if (!tab->address_focus) return true;
    if (code == KEY_ENTER) {
        navigate(tab->address, true);
        return true;
    }
    if (code == KEY_BACKSPACE) {
        usize len = tab->select_all ? 0 : kstrlen(tab->address);
        if (tab->select_all) tab->address[0] = 0;
        else if (len) tab->address[len - 1] = 0;
        tab->select_all = false;
        return true;
    }
    if (code == 0x1E && browser_ctrl) {
        tab->select_all = true;
        return true;
    }
    char ch = browser_shift ? keyboard_scancode_set1_shift[code] : keyboard_scancode_set1[code];
    if (ch >= 32 && ch <= 126) {
        usize len = tab->select_all ? 0 : kstrlen(tab->address);
        if (len < BROWSER_URL_MAX - 1) {
            if (tab->select_all) {
                tab->address[0] = 0;
                len = 0;
            }
            tab->address[len] = ch;
            tab->address[len + 1] = 0;
            tab->select_all = false;
        }
    }
    return true;
}

