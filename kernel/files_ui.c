/* files_ui.c - Read-only FAT32 root browser for the desktop.
 *
 * This surface exposes the filesystem's current real capability: root-level
 * FAT32 8.3 entries. It does not claim nested paths, LFN or write operations.
 */
#include <kernel/files_ui.h>
#include <kernel/fs.h>
#include <kernel/fb.h>
#include <kernel/gfx_text.h>
#include <kernel/font.h>
#include <kernel/ui_theme.h>
#include <kernel/serial.h>

#define FILES_CAPACITY 24u

static bool files_visible;
static fs_dirent_t files_entries[FILES_CAPACITY];
static int files_count;
static int files_selected = -1;
static bool files_available;

static u32 files_px(u32 value) { return gfx_ui_scale(value); }
static u32 files_text_h(void) { return (u32)font_cell_h * gfx_text_get_scale(); }

/* Keep drawing and pointer hit-testing on exactly the same geometry. */
static u32 files_window_x(void) {
    return files_px(36);
}
static u32 files_window_y(void) {
    return files_px(28);
}
static u32 files_window_width(void) {
    u32 margin = files_px(72);
    return g_framebuffer.width > margin ? g_framebuffer.width - margin
                                        : g_framebuffer.width;
}
static u32 files_window_height(void) {
    u32 margin = files_px(100);
    return g_framebuffer.height > margin ? g_framebuffer.height - margin
                                         : g_framebuffer.height;
}
static u32 files_title_height(void) {
    return files_px(40);
}
static u32 files_row_height(void) {
    return files_px(30);
}
static u32 files_list_y(void) {
    return files_window_y() + files_title_height() + files_px(64);
}

static void files_refresh(void) {
    files_count = fs_list_root(files_entries, FILES_CAPACITY);
    files_available = files_count >= 0;
    if (files_count < 0) files_count = 0;
    if (files_count > (int)FILES_CAPACITY) files_count = FILES_CAPACITY;
    if (files_selected >= files_count) files_selected = files_count - 1;
}

void files_open(void) {
    files_refresh();
    files_visible = true;
    serial_puts("FILES_OPEN");
    serial_printf("FILES_ROOT_COUNT=%d", files_count);
}

void files_close(void) {
    files_visible = false;
    files_selected = -1;
    serial_puts("FILES_CLOSE");
}

bool files_is_open(void) { return files_visible; }

bool files_handle_key(u8 scancode, bool pressed) {
    if (!files_visible || !pressed) return false;
    u8 code = scancode & 0x7Fu;
    if (code == 0x01u) { files_close(); return true; }
    if (code == 0x3Fu) {
        files_refresh();
        serial_puts("FILES_REFRESH");
        serial_printf("FILES_ROOT_COUNT=%d", files_count);
        return true;
    }
    if (code == 0x48u && files_count > 0) {
        if (files_selected > 0) files_selected--;
        else if (files_selected < 0) files_selected = 0;
        return true;
    }
    if (code == 0x50u && files_count > 0) {
        if (files_selected + 1 < files_count) files_selected++;
        return true;
    }
    return true;
}

bool files_handle_mouse(int mx, int my) {
    if (!files_visible) return false;
    u32 x = files_window_x(), y = files_window_y();
    u32 w = files_window_width(), h = files_window_height();
    u32 title_h = files_title_height();

    if (mx < (int)x || mx >= (int)(x + w) ||
        my < (int)y || my >= (int)(y + h)) {
        files_close();
        return true;
    }
    if (my < (int)(y + title_h) && mx >= (int)(x + w - files_px(42))) {
        files_close();
        return true;
    }

    u32 list_y = files_list_y();
    u32 row_h = files_row_height();
    if (my >= (int)list_y && row_h) {
        u32 row = (u32)(my - (int)list_y) / row_h;
        if (row < (u32)files_count && row < FILES_CAPACITY)
            files_selected = (int)row;
    }
    return true;
}

static void files_draw_size(u32 x, u32 y, u32 value) {
    char digits[11], reversed[10];
    u32 count = 0, out = 0;
    do {
        reversed[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(reversed));
    while (count) digits[out++] = reversed[--count];
    digits[out] = 0;
    gfx_draw_string(x, y, digits, CHARIS_UI_MUTED, FB_TRANSPARENT);
}

void files_render(void) {
    if (!files_visible || !g_framebuffer.initialized) return;
    u32 x = files_window_x(), y = files_window_y();
    u32 w = files_window_width(), h = files_window_height();
    u32 title_h = files_title_height(), text_h = files_text_h();
    u32 row_h = files_row_height();
    fb_fill_rect(x + files_px(4), y + files_px(4), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER);
    fb_fill_rect(x, y, w, title_h, CHARIS_UI_PANEL_RAISED);
    gfx_draw_string(x + files_px(16), y + (title_h - text_h) / 2,
                    "FILES", CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + w - files_px(28), y + (title_h - text_h) / 2,
                    "x", CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + files_px(18), y + title_h + files_px(14),
                    "ROOT VOLUME", CHARIS_UI_ACCENT, FB_TRANSPARENT);
    gfx_draw_string(x + files_px(20), y + title_h + files_px(42),
                    "NAME", CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + w - files_px(120), y + title_h + files_px(42),
                    "SIZE", CHARIS_UI_MUTED, FB_TRANSPARENT);

    if (!files_available) {
        gfx_draw_string(x + files_px(20), y + title_h + files_px(80),
                        "No readable FAT32 root volume is mounted.",
                        CHARIS_UI_WARNING, FB_TRANSPARENT);
        return;
    }
    if (!files_count) {
        gfx_draw_string(x + files_px(20), y + title_h + files_px(80),
                        "This directory is empty.", CHARIS_UI_MUTED, FB_TRANSPARENT);
    }

    /* Leave a full gap below the column labels before the first entry. */
    u32 list_y = files_list_y();
    for (int i = 0; i < files_count && i < (int)FILES_CAPACITY; i++) {
        u32 ry = list_y + (u32)i * row_h;
        if (ry + row_h > y + h - files_px(34)) break;
        if (i == files_selected)
            fb_fill_rect(x + files_px(10), ry, w - files_px(20), row_h,
                         CHARIS_UI_ACCENT_SURFACE);
        gfx_draw_string(x + files_px(20), ry + (row_h - text_h) / 2,
                        files_entries[i].name, CHARIS_UI_TEXT, FB_TRANSPARENT);
        if (files_entries[i].directory)
            gfx_draw_string(x + w - files_px(72), ry + (row_h - text_h) / 2,
                            "DIR", CHARIS_UI_ACCENT, FB_TRANSPARENT);
        files_draw_size(x + w - files_px(120), ry + (row_h - text_h) / 2,
                        files_entries[i].size);
    }
    gfx_draw_string(x + files_px(18), y + h - text_h - files_px(10),
                    "Read-only root listing | F5 refresh | Esc close | FAT32 8.3 names",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
}
