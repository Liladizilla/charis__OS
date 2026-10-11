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
        /* Treat an unqualified address as a web URL, not a search query. */
        target[0] = 'h'; target[1] = 't'; target[2] = 't'; target[3] = 'p';
        target[4] = 's'; target[5] = ':'; target[6] = '/'; target[7] = '/';
        kstrncpy(target + 8, raw, sizeof(target) - 9);
        target[sizeof(target) - 1] = 0;
    }

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
    } else if (kstrncmp(target, "http://", 7) == 0 ||
               kstrncmp(target, "https://", 8) == 0) {
        set_status("Offline: TCP/IP, DNS, TLS and HTTP engine not implemented");
    } else if (kstrncmp(target, "file://", 7) == 0) {
        set_status("Local file viewer is not connected to VFS yet");
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
        gfx_draw_string(x + 24, y + 122, "Remote websites need a real TCP/IP, DNS, TLS/HTTP client and document renderer.", C_WARNING, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 152, "The current kernel network receive and TCP/IP functions are placeholders.", C_MUTED, FB_TRANSPARENT);
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
    } else if (kstrncmp(address, "http://", 7) == 0 || kstrncmp(address, "https://", 8) == 0) {
        gfx_draw_string(x + 24, y + 22, "CAN'T REACH THIS PAGE YET", C_WARNING, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 60, address, C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 104, "The browser interface is ready, but remote web transport is not.", C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 134, "CharisOS still needs TCP/IP, DNS, TLS/HTTP and an HTML/CSS renderer.", C_MUTED, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 164, "This is a real capability gap, not a fake loading spinner.", C_WARNING, FB_TRANSPARENT);
    } else {
        gfx_draw_string(x + 24, y + 22, "LOCAL FILE VIEWER NOT READY", C_WARNING, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 60, address, C_TEXT, FB_TRANSPARENT);
        gfx_draw_string(x + 24, y + 98, "The browser's file:// route still needs safe VFS read integration.", C_MUTED, FB_TRANSPARENT);
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
    if (my >= (int)(page_y + 120)) {
        u32 rel_y = (u32)my - page_y;
        if (kstrcmp(tab->address, "charis://home") == 0) {
            if (rel_y >= 126 && rel_y < 220) {
                u32 card_w = (w - 72) / 3;
                if (card_w > 300) card_w = 300;
                if (mx < (int)(x + 24 + card_w + 12)) navigate("charis://about", true);
                else if (mx < (int)(x + 48 + card_w * 2 + 12)) navigate("charis://help", true);
                else navigate("charis://bookmarks", true);
            }
        } else if (kstrcmp(tab->address, "charis://bookmarks") == 0 && browser_has_bookmark) {
            navigate(browser_bookmark, true);
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
