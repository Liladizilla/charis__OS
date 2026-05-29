/* display.h - Multi-monitor display management */
#pragma once
#include <kernel/types.h>

#define MAX_MONITORS 4

typedef struct {
    int width, height;
    int bpp;
    u32* framebuffer;
    bool active;
} monitor_t;

extern monitor_t g_monitors[MAX_MONITORS];
extern int g_monitor_count;

void display_init(void);
int display_add_monitor(int width, int height, int bpp, u32* fb);
void display_set_primary(int index);
int display_get_primary(void);
void display_render_all(void);