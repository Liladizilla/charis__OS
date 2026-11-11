#include <kernel/memory.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/bootmem.h>
#include <kernel/fb.h>
#include <kernel/serial.h>

// Kernel heap virtual region.
#define HEAP_START 0xFFFF900000000000ULL
#define HEAP_SIZE  (1024ULL * 1024ULL * 4ULL) // 4MB

// Single memory init entry path.
void memory_init(multiboot_info_t* info) {
    // Parse the tag list first: pmm_init() needs the memory map, and the
    // graphics driver needs the framebuffer description out of the same pass.
    serial_puts("[BOOT] memory: Multiboot parsing begin");
    bootmem_parse_multiboot(info);
    u32 region_count = 0;
    (void)bootmem_regions(&region_count);
    serial_printf("[BOOT] memory: Multiboot parsing complete, regions=%u\\n",
                  region_count);
    if (!region_count) {
        serial_puts("[BOOT] FATAL: no usable Multiboot memory map");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }

    serial_puts("[BOOT] memory: PMM initialization begin");
    pmm_init(info);
    serial_printf("[BOOT] memory: PMM ready, free=%u total=%u pages\\n",
                  pmm_free_pages(), pmm_total_pages());

    serial_puts("[BOOT] memory: VMM initialization begin");
    vmm_init();
    serial_puts("[BOOT] memory: VMM initialization complete");

    // Reserve the framebuffer explicitly. Firmware normally marks it
    // non-available, but if it does not, the allocator would happily hand
    // those pages out later and the display would start showing kernel data.
    if (boot_info.has_framebuffer) {
        u64 fb_bytes = (u64)boot_info.framebuffer_pitch * boot_info.framebuffer_height;
        /* pmm_reserve_range() rounds both ends, including an unaligned
         * framebuffer base, so the final partial page cannot be allocated. */
        pmm_reserve_range(boot_info.framebuffer_addr, fb_bytes);
    }
    serial_puts("[BOOT] memory: framebuffer reservation complete");

    // Map heap pages into the higher-half heap region.
    serial_puts("[BOOT] memory: heap mapping begin");
    u32 heap_page_index = 0;
    for (u64 addr = HEAP_START; addr < HEAP_START + HEAP_SIZE; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) {
            serial_printf("[BOOT] FATAL: heap frame allocation failed at page %u\\n",
                          heap_page_index);
            for (;;) asm volatile("cli; hlt" : : : "memory");
        }
        if (!vmm_map_page(addr, phys, PTE_WRITABLE)) {
            serial_printf("[BOOT] FATAL: heap mapping failed at page %u\\n",
                          heap_page_index);
            pmm_free_page(phys);
            for (;;) asm volatile("cli; hlt" : : : "memory");
        }
        heap_page_index++;
    }

    heap_init((void*)HEAP_START, (usize)HEAP_SIZE);
    serial_printf("[BOOT] memory: heap ready, mapped=%u pages\\n",
                  heap_page_index);
}

