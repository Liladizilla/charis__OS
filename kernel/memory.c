#include <kernel/memory.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

// Kernel heap virtual region.
#define HEAP_START 0xFFFF900000000000ULL
#define HEAP_SIZE  (1024ULL * 1024ULL * 4ULL) // 4MB

// Single memory init entry path.
void memory_init(multiboot_info_t* info) {
    pmm_init(info);
    vmm_init();

    // Map heap pages into the higher-half heap region.
    for (u64 addr = HEAP_START; addr < HEAP_START + HEAP_SIZE; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) {
            // If heap can’t be mapped, fail hard.
            while (1) asm volatile("hlt");
        }
        (void)vmm_map_page(addr, phys, PTE_WRITABLE);
    }

    heap_init((void*)HEAP_START, (usize)HEAP_SIZE);
}

