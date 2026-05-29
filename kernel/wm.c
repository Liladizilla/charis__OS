#include <kernel/wm.h>
#include <kernel/graphics.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/input.h>

window_t* wm_windows = NULL;
static int wm_z_counter = 0;

void wm_init(void) {
    wm_windows = NULL;
    wm_z_counter = 0;
    g_graphics.bg_color = 0x0D1117; // Dark background (CharisOS theme)
    fb_clear(FB_COLOR(0x0D, 0x11, 0x17));
}

window_t* wm_create_window(const char* title, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return NULL;
    
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    
    kmemset(win, 0, sizeof(window_t));
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->z_order = wm_z_counter++;
    win->visible = true;
    win->focused = false;
    
    kstrncpy(win->title, title, 127);
    
    // Allocate pixel buffer for window
    win->buffer = (u32*)kmalloc(w * h * sizeof(u32));
    if (!win->buffer) {
        kfree(win);
        return NULL;
    }
    
    // Add to window list
    win->next = wm_windows;
    wm_windows = win;
    
    wm_focus_window(win);
    
    return win;
}

void wm_destroy_window(window_t* win) {
    if (!win) return;
    
    if (win->buffer) kfree(win->buffer);
    kfree(win);
}

void wm_focus_window(window_t* win) {
    if (!win) return;
    
    win->focused = true;
    win->z_order = wm_z_counter++;
    
    // Notify other windows lost focus
    for (window_t* w = wm_windows; w; w = w->next) {
        if (w != win) {
            w->focused = false;
        }
    }
}

void wm_render(void) {
    // Clear screen (desktop background)
    fb_clear(FB_COLOR(0x0D, 0x11, 0x17)); // Dark navy
    
    // Draw windows from back to front (by z_order)
    // Simple insertion sort
    window_t* sorted = NULL;
    
    for (window_t* win = wm_windows; win; ) {
        window_t* next = win->next;
        
        // Insert into sorted list
        if (!sorted || win->z_order < sorted->z_order) {
            win->next = sorted;
            sorted = win;
        } else {
            window_t* curr = sorted;
            while (curr->next && curr->next->z_order <= win->z_order) {
                curr = curr->next;
            }
            win->next = curr->next;
            curr->next = win;
        }
        win = next;
    }
    
    // Draw each window
    for (window_t* win = sorted; win; win = win->next) {
        if (win->visible) {
            wm_draw_decorations(win);
            
            // Blit window buffer to screen
            for (int py = 0; py < win->height; py++) {
                for (int px = 0; px < win->width; px++) {
                    u32 color = win->buffer[py * win->width + px];
                    fb_put_pixel(win->x + px, win->y + py + WINDOW_TITLEBAR_HEIGHT, color);
                }
            }
        }
    }
}

void wm_dispatch_event(int event_type, void* data) {
    window_t* focused = wm_get_focused();
    if (focused && focused->on_event) {
        focused->on_event(focused, event_type, data);
    }
}

window_t* wm_get_focused(void) {
    for (window_t* win = wm_windows; win; win = win->next) {
        if (win->focused) return win;
    }
    return NULL;
}

window_t* wm_get_window_at(int x, int y) {
    for (window_t* win = wm_windows; win; win = win->next) {
        if (win->visible && x >= win->x && x < win->x + win->width &&
            y >= win->y && y < win->y + win->height) {
            return win;
        }
    }
    return NULL;
}

void wm_draw_contents(window_t* win) {
    if (!win || !win->visible) return;
    
    if (win->on_paint) {
        win->on_paint(win);
    } else {
        // Default: fill with surface color
        graphics_set_color(COLOR_SURFACE);
        graphics_rect(win->x, win->y + WINDOW_TITLEBAR_HEIGHT, win->width, win->height, true);
    }
}

void wm_process_mouse(int x, int y, int button, bool pressed) {
    static bool dragging = false;
    static int drag_offset_x = 0, drag_offset_y = 0;
    static window_t* drag_window = NULL;
    
    window_t* win = wm_get_window_at(x, y);
    
    if (button == 1 && pressed) {
        // Left click - focus window
        if (win) {
            wm_focus_window(win);
            // Check if click is on titlebar for dragging
            if (y >= win->y && y < win->y + WINDOW_TITLEBAR_HEIGHT) {
                dragging = true;
                drag_window = win;
                drag_offset_x = x - win->x;
                drag_offset_y = y - win->y;
            }
            wm_render();
        }
    } else if (button == 1 && !pressed) {
        dragging = false;
        drag_window = NULL;
    }
    
    // Handle dragging
    if (dragging && drag_window && button == 1) {
        drag_window->x = x - drag_offset_x;
        drag_window->y = y - drag_offset_y;
        wm_render();
    }
}

void wm_main_loop(void) {
    while (1) {
        input_process_events();
        
        input_event_t evt;
        while (input_pop_event(&evt)) {
            switch (evt.type) {
                case INPUT_MOUSE_MOVE:
                    if (evt.mouse.button > 0) {
                        wm_process_mouse(evt.mouse.x, evt.mouse.y, evt.mouse.button, true);
                    }
                    break;
                default:
                    break;
            }
        }
        
        asm volatile("hlt");
    }
}

// CharisOS theme colors
#define COLOR_SURFACE 0x161B22
#define COLOR_BORDER  0x21262D
#define COLOR_ACCENT  0x58A6FF

void wm_draw_decorations(window_t* win) {
    if (!win->visible) return;
    
    // Titlebar (blue accent)
    graphics_set_color(COLOR_SURFACE);
    graphics_rect(win->x, win->y, win->width, WINDOW_TITLEBAR_HEIGHT, true);
    
    // Border
    graphics_set_color(COLOR_BORDER);
    graphics_rect(win->x, win->y, win->width, win->height + WINDOW_TITLEBAR_HEIGHT, false);
    
    // Title text
    graphics_set_color(COLOR_ACCENT);
    psf_draw_string(win->x + 8, win->y + 6, win->title, COLOR_ACCENT, COLOR_SURFACE);
}