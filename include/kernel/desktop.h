/* desktop.h - CharisOS desktop environment */
#pragma once
#include <kernel/types.h>

#define DESKTOP_HEIGHT 28
#define TASKBAR_HEIGHT 32
#define ICON_SIZE 32

typedef struct {
    int x, y;
    int width, height;
    char title[64];
    void* icon_data;
    bool visible;
} desktop_icon_t;

typedef struct {
    int x, y;
    int width, height;
    char title[128];
    bool visible;
} taskbar_window_t;

extern desktop_icon_t* g_desktop_icons;
extern int g_icon_count;

void desktop_init(void);
void desktop_render(void);
void desktop_add_icon(int x, int y, const char* title, void* icon);
void desktop_click_icon(int x, int y);

void taskbar_init(void);
void taskbar_render(void);
void taskbar_add_window(const char* title);
void taskbar_remove_window(const char* title);