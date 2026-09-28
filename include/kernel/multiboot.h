/* multiboot.h - Multiboot2 boot information structures
 *
 * Layout follows the Multiboot2 specification. The previous definition here
 * read mmap_length from offset 0x08 and a u32 mmap_addr from 0x0C; in reality
 * 0x08 holds tags_addr and 0x0C holds boot_loader_name, so the physical memory
 * manager was walking a pointer into the tag area and deriving a bogus entry
 * count from it. Nothing it marked as reserved was the real reserved map.
 */
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <kernel/types.h>

#define MULTIBOOT_MEMORY_AVAILABLE     1
#define MULTIBOOT_MEMORY_RESERVED      2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS           4
#define MULTIBOOT_MEMORY_BADRAM        5

/* Tag types (the ones this kernel cares about). */
#define MULTIBOOT_TAG_END              0
#define MULTIBOOT_TAG_CMDLINE          1
#define MULTIBOOT_TAG_BOOT_LOADER_NAME 2
#define MULTIBOOT_TAG_MODULE           3
#define MULTIBOOT_TAG_BASIC_MEMINFO    4
#define MULTIBOOT_TAG_BOOTDEV          5
#define MULTIBOOT_TAG_MMAP             6
#define MULTIBOOT_TAG_FRAMEBUFFER      8

/* Framebuffer colour types. */
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA     2

typedef struct {
    u32 type;
    u32 size;
    u64 addr;
    u64 len;
    u32 zero;
} __attribute__((packed)) multiboot_memory_map_t;

/* A memory-map entry as the kernel uses it internally. This is the form the
 * mmap tag is decoded into, and the form the physical memory manager walks. */
typedef struct {
    u64 base;
    u64 length;
    u32 type;
    u32 reserved;
} PACKED mem_map_entry_t;

/* Information block, byte offsets per the spec. */
typedef struct {
    u32 total_size;          /* 0x00 */
    u32 reserved;            /* 0x04 */
    u32 tags_addr;           /* 0x08 offset from this struct to the tag list */
    u32 boot_loader_name;    /* 0x0C offset from this struct, as a C string   */
    u32 cmdline;             /* 0x10 */
    u32 mods_count;          /* 0x14 */
    u32 mods_addr;           /* 0x18 */
    u32 syms;                /* 0x1C */
    u32 mmap_length;         /* 0x20 length of the memory map, in bytes      */
    u64 mmap_addr;           /* 0x24 physical address of the memory map      */
} __attribute__((packed)) multiboot_info_t;

/* Common tag header: every tag is aligned to 8 bytes and starts this way. */
typedef struct {
    u32 type;
    u32 size;
} __attribute__((packed)) multiboot_tag_t;

/*
 * Framebuffer tag (type 8).
 *
 * For type 1 (direct RGB) the three position fields give the bit offset of the
 * red, green and blue components within a pixel, so the pixel layout is not
 * assumed to be the usual BGRX.
 */
typedef struct {
    u32 type;
    u32 size;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8  framebuffer_bpp;
    u8  framebuffer_type;
    u16 reserved;
    u8  framebuffer_red_field_position;
    u8  framebuffer_green_field_position;
    u8  framebuffer_blue_field_position;
} __attribute__((packed)) multiboot_tag_framebuffer_t;

/* Basic memory info tag (type 4) — used as a fallback if the memory map
 * is somehow absent. */
typedef struct {
    u32 type;
    u32 size;
    u32 mem_lower;
    u32 mem_upper;
} __attribute__((packed)) multiboot_tag_basic_meminfo_t;

#endif
