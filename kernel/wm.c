/* wm.c - bounded native window manager */
#include <kernel/wm.h>
#include <kernel/graphics.h>
#include <kernel/memory.h>
#include <kernel/fb.h>
#include <kernel/vga.h>
#include <kernel/input.h>
#include <kernel/mouse.h>
#include <kernel/string.h>
#include <kernel/psf.h>

#define WM_MAX_DIMENSION 4096u
#define WM_MAX_BUFFER_BYTES (16ULL * 1024ULL * 1024ULL)

window_t* wm_windows = NULL;
static u32 wm_z_counter = 0;

/* Collect unique nodes from the bounded window list. The normal list
 * contains at most MAX_WINDOWS entries; the duplicate check also prevents a
 * corrupted cycle from making rendering, focus or lookup paths spin forever
 * or count the same window repeatedly. */
static u32 wm_collect_windows(window_t* windows[MAX_WINDOWS]) {
    u32 count = 0;
    for (window_t* w = wm_windows; w && count < MAX_WINDOWS; w = w->next) {
        bool seen = false;
        for (u32 i = 0; i < count; i++) {
            if (windows[i] == w) {
                seen = true;
                break;
            }
        }
        if (seen) break;
        windows[count++] = w;
    }
    return count;
}

static bool wm_contains(const window_t* target) {
    if (!target) return false;
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    for (u32 i = 0; i < count; i++)
        if (windows[i] == target) return true;
    return false;
}

static u32 wm_count(void) {
    window_t* windows[MAX_WINDOWS];
    return wm_collect_windows(windows);
}

u32 wm_get_window_count(void) {
    return wm_count();
}

static void wm_compact_z(void) {
    window_t* ordered[MAX_WINDOWS];
    u32 count = wm_collect_windows(ordered);
    for (u32 i = 1; i < count; i++) {
        window_t* item = ordered[i];
        u32 j = i;
        while (j > 0 && ordered[j - 1]->z_order > item->z_order) {
            ordered[j] = ordered[j - 1];
            j--;
        }
        ordered[j] = item;
    }
    for (u32 i = 0; i < count; i++) ordered[i]->z_order = i;
    wm_z_counter = count;
}

static void wm_raise(window_t* win) {
    if (wm_z_counter == 0xFFFFFFFFu) wm_compact_z();
    win->z_order = wm_z_counter++;
}

static u32 wm_titlebar_height(const window_t* win) {
    return win && win->titlebar_height ? win->titlebar_height : WINDOW_TITLEBAR_HEIGHT;
}

static void wm_clamp_position(window_t* win) {
    if (!win) return;
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;
    if (!g_framebuffer.initialized) return;
    s32 max_x = (g_framebuffer.width > (u32)win->width)
        ? (s32)(g_framebuffer.width - (u32)win->width) : 0;
    s32 max_y = (g_framebuffer.height > (u32)win->height + wm_titlebar_height(win))
        ? (s32)(g_framebuffer.height - ((u32)win->height + wm_titlebar_height(win))) : 0;
    if (win->x > max_x) win->x = max_x;
    if (win->y > max_y) win->y = max_y;
}

static void wm_focus_topmost(void) {
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    window_t* candidate = NULL;
    for (u32 i = 0; i < count; i++) {
        window_t* w = windows[i];
        if (w->visible && !w->minimized &&
            (!candidate || w->z_order > candidate->z_order))
            candidate = w;
    }
    if (candidate) wm_focus_window(candidate);
}

void wm_init(void) {
    wm_windows = NULL;
    wm_z_counter = 0;
    g_graphics.bg_color = CHARIS_UI_BACKGROUND;
    fb_reset_clip();
    fb_clear(CHARIS_UI_BACKGROUND);
}

static window_t* wm_create_window_internal(const char* title, int x, int y,
                                            int w, int h, bool allocate_buffer) {
    if (w <= 0 || h <= 0 || (u32)w > WM_MAX_DIMENSION ||
        (u32)h > WM_MAX_DIMENSION || wm_count() >= MAX_WINDOWS) return NULL;
    if (g_framebuffer.initialized &&
        ((u32)w > g_framebuffer.width ||
         (u64)(u32)h + WINDOW_TITLEBAR_HEIGHT > g_framebuffer.height)) return NULL;

    u64 bytes = 0;
    if (allocate_buffer) {
        u64 pixels = (u64)(u32)w * (u64)(u32)h;
        if (pixels > WM_MAX_BUFFER_BYTES / sizeof(u32)) return NULL;
        bytes = pixels * sizeof(u32);
        if (bytes > (u64)(usize)-1) return NULL;
    }

    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    kmemset(win, 0, sizeof(window_t));
    if (allocate_buffer) {
        win->buffer = (u32*)kmalloc((usize)bytes);
        if (!win->buffer) {
            kfree(win);
            return NULL;
        }
        kmemset(win->buffer, 0, (usize)bytes);
    }
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->titlebar_height = WINDOW_TITLEBAR_HEIGHT;
    win->visible = true;
    kstrncpy(win->title, title, WM_MAX_TITLE_LENGTH);
    win->title[sizeof(win->title) - 1] = '\0';
    for (u32 i = 0; i < sizeof(win->title) - 1 && win->title[i]; i++)
        if ((u8)win->title[i] < 0x20u || (u8)win->title[i] == 0x7Fu)
            win->title[i] = ' ';

    wm_clamp_position(win);
    win->next = wm_windows;
    wm_windows = win;
    wm_focus_window(win);
    return win;
}

window_t* wm_create_window(const char* title, int x, int y, int w, int h) {
    return wm_create_window_internal(title, x, y, w, h, true);
}

window_t* wm_create_surface_window(const char* title, int x, int y, int w, int h) {
    return wm_create_window_internal(title, x, y, w, h, false);
}

void wm_destroy_window(window_t* win) {
    if (!win) return;
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    u32 index = count;
    for (u32 i = 0; i < count; i++) {
        if (windows[i] == win) {
            index = i;
            break;
        }
    }
    if (index == count) return;

    bool was_focused = win->focused;

    /* Rebuild the remaining chain from the unique-node snapshot before
     * freeing the target. Besides ordinary deletion, this repairs a cycle
     * instead of leaving a self-link or dangling next pointer behind. */
    for (u32 i = index; i + 1 < count; i++)
        windows[i] = windows[i + 1];
    count--;
    wm_windows = count ? windows[0] : NULL;
    for (u32 i = 0; i < count; i++)
        windows[i]->next = (i + 1 < count) ? windows[i + 1] : NULL;

    win->next = NULL;
    win->focused = false;
    if (win->buffer) kfree(win->buffer);
    win->buffer = NULL;
    kfree(win);
    if (was_focused) wm_focus_topmost();
}

void wm_focus_window(window_t* win) {
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    bool owned = false;
    for (u32 i = 0; i < count; i++)
        if (windows[i] == win) { owned = true; break; }
    if (!owned || !win->visible || win->minimized) return;
    for (u32 i = 0; i < count; i++) windows[i]->focused = false;
    win->focused = true;
    wm_raise(win);
}

bool wm_move_window(window_t* win, int x, int y) {
    if (!wm_contains(win) || !win->visible || win->minimized) return false;
    win->x = x;
    win->y = y;
    wm_clamp_position(win);
    return true;
}

bool wm_resize_window(window_t* win, int width, int height) {
    if (!wm_contains(win) || width <= 0 || height <= 0 ||
        (u32)width > WM_MAX_DIMENSION || (u32)height > WM_MAX_DIMENSION)
        return false;
    if (g_framebuffer.initialized &&
        ((u32)width > g_framebuffer.width ||
         (u64)(u32)height + wm_titlebar_height(win) > g_framebuffer.height))
        return false;

    u32* replacement = NULL;
    if (win->buffer) {
        u64 pixels = (u64)(u32)width * (u64)(u32)height;
        if (pixels > WM_MAX_BUFFER_BYTES / sizeof(u32)) return false;
        u64 bytes = pixels * sizeof(u32);
        if (bytes > (u64)(usize)-1) return false;
        replacement = (u32*)kmalloc((usize)bytes);
        if (!replacement) return false;
        kmemset(replacement, 0, (usize)bytes);
        u32 copy_w = (u32)(width < win->width ? width : win->width);
        u32 copy_h = (u32)(height < win->height ? height : win->height);
        for (u32 row = 0; row < copy_h; row++)
            for (u32 col = 0; col < copy_w; col++)
                replacement[(usize)row * (usize)width + col] =
                    win->buffer[(usize)row * (usize)win->width + col];
    }
    if (win->buffer) kfree(win->buffer);
    win->buffer = replacement;
    win->width = width;
    win->height = height;
    wm_clamp_position(win);
    return true;
}

bool wm_set_titlebar_height(window_t* win, u32 height) {
    if (!wm_contains(win) || height < 20u || height > 96u) return false;
    if (g_framebuffer.initialized &&
        (u64)(u32)win->height + height > g_framebuffer.height) return false;
    win->titlebar_height = height;
    wm_clamp_position(win);
    return true;
}

bool wm_minimize_window(window_t* win) {
    if (!wm_contains(win) || win->minimized) return false;
    win->minimized = true;
    win->focused = false;
    if (wm_get_focused() == NULL) wm_focus_topmost();
    return true;
}

bool wm_restore_window(window_t* win) {
    if (!wm_contains(win)) return false;
    win->visible = true;
    win->minimized = false;
    wm_focus_window(win);
    return true;
}

bool wm_set_window_visible(window_t* win, bool visible) {
    if (!wm_contains(win) || win->visible == visible) return false;
    win->visible = visible;
    if (!visible && win->focused) {
        win->focused = false;
        wm_focus_topmost();
    } else if (visible && win->minimized) {
        win->minimized = false;
        wm_focus_window(win);
    }
    return true;
}

window_t* wm_focus_next(void) {
    window_t* current = wm_get_focused();
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    window_t* next = NULL;
    u32 lowest = 0xFFFFFFFFu;
    u32 current_z = current ? current->z_order : 0xFFFFFFFFu;
    for (u32 i = 0; i < count; i++) {
        window_t* w = windows[i];
        if (!w->visible || w->minimized || w == current) continue;
        if (w->z_order > current_z && w->z_order < lowest) {
            next = w;
            lowest = w->z_order;
        }
    }
    if (!next) {
        lowest = 0xFFFFFFFFu;
        for (u32 i = 0; i < count; i++) {
            window_t* w = windows[i];
            if (w->visible && !w->minimized && w != current &&
                w->z_order < lowest) {
                next = w;
                lowest = w->z_order;
            }
        }
    }
    if (next) wm_focus_window(next);
    return next;
}

void wm_render(void) {
    if (!g_framebuffer.initialized) return;
    fb_reset_clip();
    fb_clear(CHARIS_UI_BACKGROUND);
    window_t* ordered[MAX_WINDOWS];
    u32 count = wm_collect_windows(ordered);
    for (u32 i = 1; i < count; i++) {
        window_t* item = ordered[i];
        u32 j = i;
        while (j > 0 && ordered[j - 1]->z_order > item->z_order) {
            ordered[j] = ordered[j - 1];
            j--;
        }
        ordered[j] = item;
    }
    for (u32 i = 0; i < count; i++) {
        window_t* win = ordered[i];
        if (!win->visible || win->minimized || !win->buffer) continue;
        u32 title_h = wm_titlebar_height(win);
        u32 clip_h = (u32)win->height + title_h;
        fb_set_clip((u32)win->x, (u32)win->y, (u32)win->width, clip_h);
        wm_draw_decorations(win);
        wm_draw_contents(win);
        for (u32 py = 0; py < (u32)win->height; py++)
            for (u32 px = 0; px < (u32)win->width; px++) {
                u32 color = win->buffer[(usize)py * (usize)win->width + px];
                fb_put_pixel((u32)win->x + px,
                             (u32)win->y + title_h + py, color);
            }
    }
    fb_reset_clip();
}

/* Render window-owned application surfaces over the shell workspace without
 * clearing the desktop or taskbar. A bufferless window with on_paint is a
 * native app surface: its renderer owns its complete chrome and content.
 * Legacy buffer-backed windows keep the existing WM decoration path. */
void wm_render_surfaces(void) {
    if (!g_framebuffer.initialized) return;
    window_t* ordered[MAX_WINDOWS];
    u32 count = wm_collect_windows(ordered);
    for (u32 i = 1; i < count; i++) {
        window_t* item = ordered[i];
        u32 j = i;
        while (j > 0 && ordered[j - 1]->z_order > item->z_order) {
            ordered[j] = ordered[j - 1];
            j--;
        }
        ordered[j] = item;
    }

    fb_reset_clip();
    for (u32 i = 0; i < count; i++) {
        window_t* win = ordered[i];
        if (!win->visible || win->minimized) continue;
        u32 title_h = wm_titlebar_height(win);
        u32 clip_h = (u32)win->height + title_h;
        fb_set_clip((u32)win->x, (u32)win->y, (u32)win->width, clip_h);
        if (win->on_paint) {
            win->on_paint(win);
        } else {
            wm_draw_decorations(win);
            wm_draw_contents(win);
            if (win->buffer) {
                for (u32 py = 0; py < (u32)win->height; py++)
                    for (u32 px = 0; px < (u32)win->width; px++) {
                        u32 color = win->buffer[(usize)py * (usize)win->width + px];
                        fb_put_pixel((u32)win->x + px,
                                     (u32)win->y + title_h + py,
                                     color);
                    }
            }
        }
        fb_reset_clip();
    }
}

void wm_dispatch_event(int event_type, void* data) {
    window_t* focused = wm_get_focused();
    if (focused && focused->on_event)
        focused->on_event(focused, event_type, data);
}

window_t* wm_get_focused(void) {
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    for (u32 i = 0; i < count; i++) {
        window_t* win = windows[i];
        if (win->focused && win->visible && !win->minimized) return win;
    }
    return NULL;
}

window_t* wm_get_window_at(int x, int y) {
    window_t* best = NULL;
    window_t* windows[MAX_WINDOWS];
    u32 count = wm_collect_windows(windows);
    for (u32 i = 0; i < count; i++) {
        window_t* win = windows[i];
        if (!win->visible || win->minimized) continue;
        s64 right = (s64)win->x + win->width;
        s64 bottom = (s64)win->y + win->height + wm_titlebar_height(win);
        if ((s64)x >= win->x && (s64)x < right &&
            (s64)y >= win->y && (s64)y < bottom &&
            (!best || win->z_order > best->z_order)) best = win;
    }
    return best;
}

void wm_draw_contents(window_t* win) {
    if (!win || !wm_contains(win) || !win->visible || win->minimized) return;
    if (win->on_paint) win->on_paint(win);
    else {
        graphics_set_color(COLOR_SURFACE);
        graphics_rect((u32)win->x, (u32)win->y + wm_titlebar_height(win),
                      (u32)win->width, (u32)win->height, true);
    }
}

static void wm_move_to(window_t* win, int x, int y) {
    (void)wm_move_window(win, x, y);
}

void wm_process_mouse(int x, int y, int button, bool pressed) {
    static bool dragging = false;
    static int offset_x = 0, offset_y = 0;
    static window_t* drag_window = NULL;
    if (button == 1 && pressed) {
        window_t* win = wm_get_window_at(x, y);
        if (win) {
            wm_focus_window(win);
            if (y >= win->y && y < win->y + (int)wm_titlebar_height(win)) {
                dragging = true;
                drag_window = win;
                offset_x = x - win->x;
                offset_y = y - win->y;
            }
            wm_render();
        }
    } else if (button == 1 && !pressed) {
        dragging = false;
        drag_window = NULL;
    } else if (dragging && drag_window && wm_contains(drag_window)) {
        wm_move_to(drag_window, x - offset_x, y - offset_y);
        wm_render();
    } else {
        dragging = false;
        drag_window = NULL;
    }
}

void wm_main_loop(void) {
    static bool mouse_left_down = false;
    static int drag_offset_x = 0, drag_offset_y = 0;
    static window_t* drag_window = NULL;
    while (1) {
        input_process_events();
        input_event_t evt;
        while (input_pop_event(&evt)) {
            switch (evt.type) {
                case INPUT_MOUSE_MOVE:
                    if (mouse_left_down && drag_window && wm_contains(drag_window)) {
                        wm_move_to(drag_window, evt.mouse.x - drag_offset_x,
                                   evt.mouse.y - drag_offset_y);
                        wm_render();
                    }
                    break;
                case INPUT_MOUSE_DOWN:
                    if (evt.mouse.button == 1) {
                        mouse_left_down = true;
                        window_t* win = wm_get_window_at(evt.mouse.x, evt.mouse.y);
                        drag_window = NULL;
                        if (win) {
                            wm_focus_window(win);
                            if (evt.mouse.y >= win->y &&
                                evt.mouse.y < win->y + (int)wm_titlebar_height(win)) {
                                drag_window = win;
                                drag_offset_x = evt.mouse.x - win->x;
                                drag_offset_y = evt.mouse.y - win->y;
                            }
                            wm_render();
                        }
                    }
                    break;
                case INPUT_MOUSE_UP:
                    if (evt.mouse.button == 1) {
                        mouse_left_down = false;
                        drag_window = NULL;
                    }
                    break;
                case INPUT_KEY_DOWN: {
                    window_t* focused = wm_get_focused();
                    if (focused && focused->on_event)
                        focused->on_event(focused, 100, &evt.key.keycode);
                    break;
                }
                default: break;
            }
        }
        asm volatile("hlt");
    }
}

void wm_draw_decorations(window_t* win) {
    if (!win || !wm_contains(win) || !win->visible || win->minimized) return;
    graphics_set_color(COLOR_SURFACE);
    graphics_rect((u32)win->x, (u32)win->y, (u32)win->width,
                  wm_titlebar_height(win), true);
    graphics_set_color(COLOR_BORDER);
    graphics_rect((u32)win->x, (u32)win->y, (u32)win->width,
                  (u32)win->height + wm_titlebar_height(win), false);
    graphics_set_color(COLOR_ACCENT);
    psf_draw_string((u32)win->x + 8, (u32)win->y + 6,
                    win->title, COLOR_ACCENT, COLOR_SURFACE);
}

void wm_set_screen_bounds(uint32_t width, uint32_t height) {
    mouse_set_bounds(width, height);
}
