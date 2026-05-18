/* memory.h - Physical and virtual memory management */
#pragma once

#include <kernel/types.h>
#include <kernel/multiboot.h>

/* Page size and alignment */
#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12
#define HUGE_PAGE_SIZE  (2ULL * 1024ULL * 1024ULL)

/* Multiboot2 memory map entry types from multiboot2 */
#define MEM_AVAILABLE       1
#define MEM_RESERVED        2
#define MEM_ACPI_RECLAIM    3
#define MEM_NVS             4
#define MEM_BADRAM          5

/* Multiboot2 memory map entry */
typedef struct {
    u64 base;
    u64 length;
    u32 type;
    u32 reserved;
} PACKED mem_map_entry_t;

/* Physical memory manager (page allocator) */
void pmm_init(multiboot_info_t* info);
u64 pmm_alloc_page(void);
void pmm_free_page(u64 addr);

/* Kernel heap */
void heap_init(void* start, usize size);
void* kmalloc(usize size);
void kfree(void* ptr);

/* Virtual memory manager (paging) */
void vmm_init(void);
bool vmm_map_page(u64 virt, u64 phys, u64 flags);
void vmm_unmap_page(u64 virt);
u64 vmm_get_phys(u64 virt);

/* Initialization function (single entry path) */
void memory_init(multiboot_info_t* info);

/* Page table flags */
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_WRITETHROUGH (1ULL << 3)
#define PTE_NOCACHE     (1ULL << 4)
#define PTE_ACCESSED    (1ULL << 5)
#define PTE_DIRTY       (1ULL << 6)
#define PTE_HUGE        (1ULL << 7)
#define PTE_GLOBAL      (1ULL << 8)
#define PTE_NX          (1ULL << 63)

/* Higher half kernel offset (optional future use) */
#define HH_OFFSET       0xFFFF800000000000ULL

