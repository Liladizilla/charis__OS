#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <kernel/types.h>

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS 4
#define MULTIBOOT_MEMORY_BADRAM 5

typedef struct {
    u32 type;
    u32 size;
    u64 addr;
    u64 len;
    u32 zero;
} __attribute__((packed)) multiboot_memory_map_t;

typedef struct {
    u32 total_size;
    u32 reserved;
    /* ... other fields ... */
    u32 mmap_length;
    u32 mmap_addr;
    /* ... */
} __attribute__((packed)) multiboot_info_t;

#endif