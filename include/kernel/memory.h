/* memory.h - Physical and virtual memory management */
#pragma once
#include <kernel/types.h>

/* Page size and alignment */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12
#define HUGE_PAGE_SIZE  (2 * 1024 * 1024)

/* Memory map entry types from multiboot2 */
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

/* Physical memory manager (bitmap-based) */
void pmm_init(u64 total_mem, mem_map_entry_t* entries, u32 num_entries);
void* pmm_alloc_frame(void);
void pmm_free_frame(void* addr);
extern u64 pmm_total_frames;
extern u64 pmm_used_frames;

/* Kernel heap */
void heap_init(void* start, usize size);
void* kmalloc(usize size);
void* krealloc(void* ptr, usize size);
void kfree(void* ptr);

/* Virtual memory manager (paging) */
void vmm_init(void);
void* vmm_alloc_page(void);
void vmm_free_page(void* virt);
void vmm_map_page(void* phys, void* virt, u64 flags);
void vmm_unmap_page(void* virt);
void* vmm_get_phys(void* virt);

/* Initialization function */
void memory_init(u32 info);

/* Page table flags */
#define PTE_PRESENT     (1 << 0)
#define PTE_WRITABLE    (1 << 1)
#define PTE_USER        (1 << 2)
#define PTE_WRITETHROUGH (1 << 3)
#define PTE_NOCACHE     (1 << 4)
#define PTE_ACCESSED    (1 << 5)
#define PTE_DIRTY       (1 << 6)
#define PTE_HUGE        (1 << 7)
#define PTE_GLOBAL      (1 << 8)
#define PTE_NX          (1ULL << 63)

/* Higher half kernel offset (optional future use) */
#define HH_OFFSET       0xFFFF800000000000ULL

