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

    fb_fill_rect(x + files_px(5), y + files_px(6), w, h, CHARIS_UI_SHADOW);
    fb_fill_rect(x, y, w, h, CHARIS_UI_PANEL);
    fb_draw_rect(x, y, w, h, CHARIS_UI_BORDER_STRONG);

    /* Application title bar. */
    fb_fill_rect(x, y, w, title_h, CHARIS_UI_PANEL_RAISED);
    fb_fill_rect(x, y + title_h, w, files_px(1), CHARIS_UI_BORDER);
    gfx_draw_string(x + files_px(18), y + (title_h - text_h) / 2,
                    "Files", CHARIS_UI_TEXT, FB_TRANSPARENT);
    gfx_draw_string(x + w - files_px(28), y + (title_h - text_h) / 2,
                    "x", CHARIS_UI_MUTED, FB_TRANSPARENT);

    /* Location and action toolbar. */
    u32 toolbar_y = y + title_h + files_px(10);
    fb_fill_rect(x + files_px(12), toolbar_y, w - files_px(24), files_px(38),
                 CHARIS_UI_SURFACE);
    fb_draw_rect(x + files_px(12), toolbar_y, w - files_px(24), files_px(38),
                 CHARIS_UI_BORDER);
    gfx_draw_string(x + files_px(26), toolbar_y + (files_px(38) - text_h) / 2,
                    "VOLUMES  /  ROOT", CHARIS_UI_ACCENT, FB_TRANSPARENT);
    gfx_draw_string(x + w - files_px(104), toolbar_y + (files_px(38) - text_h) / 2,
                    "F5  Refresh", CHARIS_UI_MUTED, FB_TRANSPARENT);

    /* Column header and divider. */
    u32 header_y = toolbar_y + files_px(54);
    gfx_draw_string(x + files_px(56), header_y, "NAME",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + w - files_px(154), header_y, "SIZE",
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    fb_fill_rect(x + files_px(16), header_y + text_h + files_px(8),
                 w - files_px(32), files_px(1), CHARIS_UI_BORDER);

    if (!files_available) {
        gfx_draw_string(x + files_px(24), header_y + files_px(42),
                        "No readable FAT32 volume is mounted.",
                        CHARIS_UI_WARNING, FB_TRANSPARENT);
        gfx_draw_string(x + files_px(24), header_y + files_px(68),
                        "Connect a supported volume and refresh the view.",
                        CHARIS_UI_MUTED, FB_TRANSPARENT);
    } else if (!files_count) {
        gfx_draw_string(x + files_px(24), header_y + files_px(42),
                        "This directory is empty.", CHARIS_UI_MUTED, FB_TRANSPARENT);
    }

    u32 list_y = header_y + text_h + files_px(24);
    for (int i = 0; i < files_count && i < (int)FILES_CAPACITY; i++) {
        u32 ry = list_y + (u32)i * row_h;
        if (ry + row_h > y + h - files_px(42)) break;
        if (i == files_selected)
            fb_fill_rect(x + files_px(12), ry, w - files_px(24), row_h,
                         CHARIS_UI_ACCENT_SURFACE);

        /* Compact file glyph with a folded-corner cue. */
        u32 ix = x + files_px(24), iy = ry + files_px(6);
        fb_fill_rect(ix, iy, files_px(16), files_px(18),
                     files_entries[i].directory ? 0xFF3A3021u : CHARIS_UI_SURFACE);
        fb_draw_rect(ix, iy, files_px(16), files_px(18), CHARIS_UI_BORDER_STRONG);
        fb_fill_rect(ix + files_px(4), iy + files_px(6), files_px(8), files_px(1),
                     files_entries[i].directory ? CHARIS_UI_WARNING : CHARIS_UI_MUTED);
        fb_fill_rect(ix + files_px(4), iy + files_px(10), files_px(7), files_px(1),
                     files_entries[i].directory ? CHARIS_UI_WARNING : CHARIS_UI_MUTED);

        gfx_draw_string(x + files_px(56), ry + (row_h - text_h) / 2,
                        files_entries[i].name, CHARIS_UI_TEXT, FB_TRANSPARENT);
        if (files_entries[i].directory)
            gfx_draw_string(x + w - files_px(154), ry + (row_h - text_h) / 2,
                            "Folder", CHARIS_UI_ACCENT, FB_TRANSPARENT);
        else
            files_draw_size(x + w - files_px(154), ry + (row_h - text_h) / 2,
                            files_entries[i].size);
    }

    /* Persistent footer reports real capability instead of implying write access. */
    u32 footer_y = y + h - files_px(30);
    fb_fill_rect(x + files_px(12), footer_y - files_px(8),
                 w - files_px(24), files_px(1), CHARIS_UI_BORDER);
    char count_text[24];
    u32 count = (u32)files_count, n = 0;
    char rev[10];
    do { rev[n++] = (char)('0' + count % 10u); count /= 10u; }
    while (count && n < sizeof(rev));
    u32 p = 0;
    while (n) count_text[p++] = rev[--n];
    count_text[p++] = ' '; count_text[p++] = 'i';
    count_text[p++] = 't'; count_text[p++] = 'e';
    count_text[p++] = 'm'; count_text[p] = 0;
    gfx_draw_string(x + files_px(18), footer_y, count_text,
                    CHARIS_UI_MUTED, FB_TRANSPARENT);
    gfx_draw_string(x + w - files_px(270), footer_y,
                    "Read-only  |  FAT32 8.3", CHARIS_UI_MUTED, FB_TRANSPARENT);
}
