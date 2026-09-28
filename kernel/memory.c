#include <kernel/memory.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/bootmem.h>
#include <kernel/fb.h>

// Kernel heap virtual region.
#define HEAP_START 0xFFFF900000000000ULL
#define HEAP_SIZE  (1024ULL * 1024ULL * 4ULL) // 4MB

// Single memory init entry path.
void memory_init(multiboot_info_t* info) {
    // Parse the tag list first: pmm_init() needs the memory map, and the
    // graphics driver needs the framebuffer description out of the same pass.
    bootmem_parse_multiboot(info);

    pmm_init(info);
    vmm_init();

    // Reserve the framebuffer explicitly. Firmware normally marks it
    // non-available, but if it does not, the allocator would happily hand
    // those pages out later and the display would start showing kernel data.
    if (boot_info.has_framebuffer) {
        u64 fb_bytes = (u64)boot_info.framebuffer_pitch * boot_info.framebuffer_height;
        u64 fb_start = boot_info.framebuffer_addr & ~(PAGE_SIZE - 1);
        for (u64 a = fb_start; a < fb_start + fb_bytes; a += PAGE_SIZE) {
            pmm_reserve_range(a, PAGE_SIZE);
        }
    }

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

