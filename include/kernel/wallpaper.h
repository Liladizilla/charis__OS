/* wallpaper.h - Built-in photographic and procedural desktop backgrounds. */
#ifndef KERNEL_WALLPAPER_H
#define KERNEL_WALLPAPER_H

#include <kernel/types.h>

typedef enum {
    WALLPAPER_CHARIS = 0,       /* Existing default CC0 lake photograph. */
    WALLPAPER_EMBER = 1,        /* Retained procedural theme preset. */
    WALLPAPER_FOREST = 2,       /* Retained procedural theme preset. */
    WALLPAPER_DUSK = 3,         /* Retained procedural theme preset. */
    WALLPAPER_MONO = 4,         /* Retained procedural theme preset. */
    WALLPAPER_ALPINE = 5,       /* CC0 alpine village photograph. */
    WALLPAPER_FOREST_SKY = 6,   /* CC0 forest canopy photograph. */
    WALLPAPER_CITYSCAPE = 7,    /* CC0 city photograph. */
    WALLPAPER_COUNT = 8
} wallpaper_style_t;

void        wallpaper_draw(u32 style);
const char* wallpaper_name(u32 style);
const char* wallpaper_description(u32 style);
u32         wallpaper_count(void);

#endif
