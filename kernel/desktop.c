#include <kernel/desktop.h>
#include <kernel/wm.h>
#include <kernel/graphics.h>
#include <kernel/memory.h>
#include <kernel/vga.h>

static desktop_icon_t desktop_icons[32];
static taskbar_window_t taskbar_windows[16];
desktop_icon_t* g_desktop_icons = desktop_icons;
int g_icon_count = 0;
static int taskbar_count = 0;

// CharisOS desktop theme
#define COLOR_DESKTOP_BG  0x0D1117
#define COLOR_TASKBAR_BG  0x161B22
#define COLOR_TASKBAR_FG  0x58A6FF

void desktop_init(void) {
    g_icon_count = 0;
    taskbar_count = 0;
    fb_clear(FB_COLOR(0x0D, 0x11, 0x17));
    vga_puts("Desktop initialized\n");
}

void desktop_render(void) {
    // Clear desktop background
    graphics_set_color(COLOR_DESKTOP_BG);
    graphics_rect(0, 0, 640, 480 - TASKBAR_HEIGHT, true);
    
    // Draw icons
    for (int i = 0; i < g_icon_count; i++) {
        if (desktop_icons[i].visible) {
            // Draw simple icon placeholder (colored square)
            graphics_set_color(COLOR_TASKBAR_FG);
            graphics_rect(desktop_icons[i].x, desktop_icons[i].y, 
                         ICON_SIZE, ICON_SIZE, true);
            
            // Draw icon title
            psf_draw_string(desktop_icons[i].x, desktop_icons[i].y + ICON_SIZE + 4,
                           desktop_icons[i].title, COLOR_TASKBAR_FG, COLOR_DESKTOP_BG);
        }
    }
    
    // Render taskbar at bottom
    taskbar_render();
    
    // Render windows
    wm_render();
}

void desktop_add_icon(int x, int y, const char* title, void* icon) {
    if (g_icon_count >= 32) return;
    
    desktop_icons[g_icon_count].x = x;
    desktop_icons[g_icon_count].y = y;
    desktop_icons[g_icon_count].width = ICON_SIZE;
    desktop_icons[g_icon_count].height = ICON_SIZE;
    kstrncpy(desktop_icons[g_icon_count].title, title, 63);
    desktop_icons[g_icon_count].icon_data = icon;
    desktop_icons[g_icon_count].visible = true;
    g_icon_count++;
}

void desktop_click_icon(int x, int y) {
    for (int i = 0; i < g_icon_count; i++) {
        if (desktop_icons[i].visible &&
            x >= desktop_icons[i].x && x < desktop_icons[i].x + ICON_SIZE &&
            y >= desktop_icons[i].y && y < desktop_icons[i].y + ICON_SIZE) {
            // Icon clicked - create window or launch app
            task_t* focused = wm_get_focused();
            if (!focused) {
                wm_create_window(desktop_icons[i].title, 100, 100, 300, 200);
                wm_render();
            }
        }
    }
}

void taskbar_init(void) {
    taskbar_count = 0;
}

void taskbar_render(void) {
    // Taskbar background
    graphics_set_color(COLOR_TASKBAR_BG);
    graphics_rect(0, 480 - TASKBAR_HEIGHT, 640, TASKBAR_HEIGHT, true);
    
    // Draw window buttons
    int btn_x = 8;
    for (int i = 0; i < taskbar_count; i++) {
        // Button background
        graphics_set_color(COLOR_TASKBAR_FG);
        graphics_rect(btn_x, 480 - TASKBAR_HEIGHT + 4, 100, 24, true);
        
        // Window title
        psf_draw_string(btn_x + 8, 480 - TASKBAR_HEIGHT + 10,
                       taskbar_windows[i].title, COLOR_TASKBAR_BG, COLOR_TASKBAR_FG);
        
        btn_x += 110;
    }
}

void taskbar_add_window(const char* title) {
    if (taskbar_count >= 16) return;
    
    kstrncpy(taskbar_windows[taskbar_count].title, title, 127);
    taskbar_windows[taskbar_count].visible = true;
    taskbar_count++;
}

void taskbar_remove_window(const char* title) {
    for (int i = 0; i < taskbar_count; i++) {
        if (kstrcmp(taskbar_windows[i].title, title) == 0) {
            // Shift remaining windows
            for (int j = i; j < taskbar_count - 1; j++) {
                taskbar_windows[j] = taskbar_windows[j + 1];
            }
            taskbar_count--;
            return;
        }
    }
}