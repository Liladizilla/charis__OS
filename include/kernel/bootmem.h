/* bootmem.h - Multiboot2 boot information */
#pragma once

#include <kernel/types.h>
#include <kernel/multiboot.h>

/* What the bootloader told us about the display, if anything. */
typedef struct {
    bool     has_framebuffer;
    u64      framebuffer_addr;
    u32      framebuffer_pitch;    /* bytes per scanline */
    u32      framebuffer_width;
    u32      framebuffer_height;
    u8       framebuffer_bpp;
    u8       framebuffer_type;     /* 0 indexed, 1 direct RGB, 2 EGA text */
    u8       framebuffer_red;      /* bit position of red within a pixel */
    u8       framebuffer_green;
    u8       framebuffer_blue;
    const char* cmdline;
    const char* boot_loader;
    u32       tags_offset;          /* validated position of the tag list */
    u32       region_count;         /* entries in the memory map */
} bootmem_display_t;

extern bootmem_display_t boot_info;

/* Walk the multiboot tag list and fill in boot_info. */
void bootmem_parse_multiboot(multiboot_info_t* info);

/* Physical memory map, as recovered from the mmap tag rather than from the
 * info header's offset fields, which this bootloader fills in incorrectly. */
const mem_map_entry_t* bootmem_regions(u32* count);
