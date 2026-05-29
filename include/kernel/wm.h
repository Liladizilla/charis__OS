/* wm.h - Window Manager */
#pragma once
#include <kernel/types.h>
#include <kernel/graphics.h>

#define MAX_WINDOWS 16
#define WINDOW_TITLEBAR_HEIGHT 28

typedef struct window {
    int x, y;
    int width, height;
    int z_order; // Higher = front
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
    char title[128];
    u32* buffer; // Window pixel buffer
    struct window* next;
    
    // Event callback
    void (*on_paint)(struct window*);
    void (*on_event)(struct window*, int event_type, void* data);
} window_t;

extern window_t* wm_windows; // Linked list of windows

// Initialize window manager
void wm_init(void);

// Create a new window
window_t* wm_create_window(const char* title, int x, int y, int w, int h);

// Destroy window
void wm_destroy_window(window_t* win);

// Bring window to front
void wm_focus_window(window_t* win);

// Render all windows (compositor)
void wm_render(void);

// Dispatch input event to focused window
void wm_dispatch_event(int event_type, void* data);

// Get focused window
window_t* wm_get_focused(void);

// Get window at screen position
window_t* wm_get_window_at(int x, int y);

// Draw window decorations (titlebar, borders)
void wm_draw_decorations(window_t* win);

// Draw window contents (calls on_paint if set)
void wm_draw_contents(window_t* win);

// Process mouse events for window management
void wm_process_mouse(int x, int y, int button, bool pressed);

// Main compositor loop
void wm_main_loop(void);