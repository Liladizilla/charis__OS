/* wallpaper.h - Procedural desktop backgrounds */
#ifndef KERNEL_WALLPAPER_H
#define KERNEL_WALLPAPER_H

#include <kernel/types.h>

typedef enum {
    WALLPAPER_CHARIS = 0,   /* the default */
    WALLPAPER_EMBER,
    WALLPAPER_FOREST,
    WALLPAPER_DUSK,
    WALLPAPER_MONO,
    WALLPAPER_COUNT
} wallpaper_style_t;

void        wallpaper_draw(u32 style);
const char* wallpaper_name(u32 style);
const char* wallpaper_description(u32 style);
u32         wallpaper_count(void);

#endif
