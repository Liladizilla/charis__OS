/* display.h - Active display mode supplied by the boot protocol. */
#ifndef KERNEL_DISPLAY_H
#define KERNEL_DISPLAY_H

#include <kernel/types.h>

/* This describes the single boot framebuffer currently available to the
 * kernel. It is not a monitor-discovery or mode-setting API. */
typedef struct {
    bool available;
    u64 framebuffer_physical;
    u32 width;
    u32 height;
    u32 pitch;
    u8 bpp;
    u8 fb_type;
    u8 red_pos;
    u8 green_pos;
    u8 blue_pos;
} display_state_t;

extern display_state_t g_display;

void display_init(void);

#endif
