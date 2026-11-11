#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/multiboot.h>
#include <kernel/printf.h>
#include <kernel/bootmem.h>
#include <kernel/serial.h>

#define PMM_BITMAP_BASE 0x100000ULL
#define PMM_BLOCK_SIZE  (2ULL * 1024ULL * 1024ULL)
#define PMM_PAGE_COUNT  ((VMM_PHYSICAL_LIMIT - PMM_BITMAP_BASE) / 4096ULL)
#define BITMAP_SIZE     ((PMM_PAGE_COUNT + 7ULL) / 8ULL)

static const mem_map_entry_t* memory_map;
static u32 memory_map_count;
static u8 bitmap[BITMAP_SIZE];
/* Distinguish allocated frames from permanent reservations. */
static u8 allocated_bitmap[BITMAP_SIZE];
static u32 last_free = 0;
static u32 managed_page_count = 0;
static u32 free_page_count = 0;

static void pmm_mark_used(u64 addr) {
    if (addr < PMM_BITMAP_BASE || addr >= VMM_PHYSICAL_LIMIT) return;
    u64 page = (addr - PMM_BITMAP_BASE) / 4096ULL;
    u32 byte = (u32)(page / 8ULL);
    u8 mask = (u8)(1U << (page % 8ULL));
    if (!(bitmap[byte] & mask) && free_page_count) free_page_count--;
    bitmap[byte] |= mask;
}

static void pmm_mark_free(u64 addr) {
    if (addr < PMM_BITMAP_BASE || addr >= VMM_PHYSICAL_LIMIT) return;
    u64 page = (addr - PMM_BITMAP_BASE) / 4096ULL;
    u32 byte = (u32)(page / 8ULL);
    u8 mask = (u8)(1U << (page % 8ULL));
    if ((bitmap[byte] & mask) && free_page_count < managed_page_count) {
        free_page_count++;
    }
    bitmap[byte] &= (u8)~mask;
}

static u64 pmm_align_up_page(u64 value) {
    if (value > UINT64_MAX - 4095ULL) return UINT64_MAX;
    return (value + 4095ULL) & ~4095ULL;
}


/* A 2 MiB block is direct-mappable only when it is entirely backed by usable
 * RAM and no firmware reservation or framebuffer overlaps it. */
bool pmm_block_is_usable_ram(u64 block_base) {
    if ((block_base & (PMM_BLOCK_SIZE - 1)) ||
        block_base < PMM_BITMAP_BASE ||
        block_base > VMM_PHYSICAL_LIMIT - PMM_BLOCK_SIZE) return false;

    u64 block_end = block_base + PMM_BLOCK_SIZE;
    bool covered = false;
    for (u32 i = 0; i < memory_map_count; i++) {
        const mem_map_entry_t* region = &memory_map[i];
        if (region->type != MULTIBOOT_MEMORY_AVAILABLE) continue;
        u64 end = region->base + region->length;
        if (end < region->base) continue;
        if (region->base <= block_base && end >= block_end) {
            covered = true;
            break;
        }
    }
    if (!covered) return false;

    for (u32 i = 0; i < memory_map_count; i++) {
        const mem_map_entry_t* region = &memory_map[i];
        if (region->type == MULTIBOOT_MEMORY_AVAILABLE || !region->length) continue;
        u64 end = region->base + region->length;
        if (end < region->base) end = UINT64_MAX;
        if (region->base < block_end && end > block_base) return false;
    }

    if (boot_info.has_framebuffer) {
        u64 fb_start = boot_info.framebuffer_addr;
        u64 fb_bytes = (u64)boot_info.framebuffer_pitch * boot_info.framebuffer_height;
        u64 fb_end = fb_start + fb_bytes;
        if (fb_end < fb_start) fb_end = UINT64_MAX;
        if (fb_start < block_end && fb_end > block_base) return false;
    }
    return true;
}

extern char _kernel_end[];

void pmm_init(multiboot_info_t* info) {
    serial_puts("[BOOT] PMM: initialization begin");
    memory_map = bootmem_regions(&memory_map_count);
    serial_printf("[BOOT] PMM: memory map entries=%u\n", memory_map_count);
    last_free = 0;
    managed_page_count = 0;
    free_page_count = 0;

    /* Fail closed: only complete, firmware-declared RAM blocks are eligible. */
    serial_puts("[BOOT] PMM: bitmap initialization begin");
    kmemset(bitmap, 0xFF, sizeof(bitmap));
    kmemset(allocated_bitmap, 0, sizeof(allocated_bitmap));
    serial_puts("[BOOT] PMM: bitmap initialization complete");
    u64 first_block = (PMM_BITMAP_BASE + PMM_BLOCK_SIZE - 1) &
                      ~(PMM_BLOCK_SIZE - 1);
    serial_puts("[BOOT] PMM: usable-RAM scan begin");
    u32 scanned_blocks = 0;
    for (u64 block = first_block;
         block <= VMM_PHYSICAL_LIMIT - PMM_BLOCK_SIZE;
         block += PMM_BLOCK_SIZE) {
        scanned_blocks++;
        if (!pmm_block_is_usable_ram(block)) continue;
        managed_page_count += (u32)(PMM_BLOCK_SIZE / 4096ULL);
        for (u64 addr = block; addr < block + PMM_BLOCK_SIZE; addr += 4096ULL) {
            pmm_mark_free(addr);
        }
    }

    serial_printf("[BOOT] PMM: usable-RAM scan complete, scanned=%u managed_pages=%u\n",
                  scanned_blocks, managed_page_count);

    /* Kernel image and still-referenced Multiboot tag storage remain reserved. */
    serial_puts("[BOOT] PMM: kernel image reservation begin");
    for (u64 addr = 0x100000ULL; addr < (u64)_kernel_end; addr += 4096ULL) {
        pmm_mark_used(addr);
    }
    serial_puts("[BOOT] PMM: kernel image reservation complete");
    if (info && info->total_size) {
        serial_puts("[BOOT] PMM: Multiboot info reservation begin");
        pmm_reserve_range((u64)(uintptr_t)info, info->total_size);
        serial_puts("[BOOT] PMM: Multiboot info reservation complete");
    }

    serial_printf("[BOOT] PMM: ready, free=%u total=%u pages\n",
                  pmm_free_pages(), pmm_total_pages());
    vga_printf("PMM: %u regions, %u managed pages, %u used/reserved, %u free\n",
               memory_map_count, pmm_total_pages(),
               pmm_total_pages() - pmm_free_pages(), pmm_free_pages());
}

void pmm_reserve_range(u64 addr, u64 len) {
    if (!len || addr >= VMM_PHYSICAL_LIMIT) return;

    u64 end = addr + len;
    if (end < addr || end > VMM_PHYSICAL_LIMIT) end = VMM_PHYSICAL_LIMIT;

    u64 start = addr & ~4095ULL;
    end = pmm_align_up_page(end);
    if (start < PMM_BITMAP_BASE) start = PMM_BITMAP_BASE;

    /*
     * Keep the range inside the exact page window represented by the bitmap.
     * VMM_PHYSICAL_LIMIT is the allocator's address ceiling, but the bitmap
     * size is the authority for indexing its backing arrays. The explicit
     * page/byte checks also keep newer GCC range analysis from treating a
     * narrowed u32 index as potentially wrapped.
     */
    const u64 bitmap_end =
        PMM_BITMAP_BASE + (u64)PMM_PAGE_COUNT * 4096ULL;
    if (end > bitmap_end) end = bitmap_end;
    if (end > VMM_PHYSICAL_LIMIT) end = VMM_PHYSICAL_LIMIT;

    for (u64 current = start; current < end; current += 4096ULL) {
        u64 page = (current - PMM_BITMAP_BASE) / 4096ULL;
        if (page >= PMM_PAGE_COUNT) break;

        u64 byte_index = page / 8ULL;
        if (byte_index >= (u64)BITMAP_SIZE) break;

        u8 mask = (u8)(1U << (page % 8ULL));
        pmm_mark_used(current);
        allocated_bitmap[(usize)byte_index] &= (u8)~mask;
    }
}

u64 pmm_alloc_page(void) {
    for (u32 i = last_free; i < BITMAP_SIZE; i++) {
        if (bitmap[i] == 0xFF) continue;

        for (u8 bit = 0; bit < 8; bit++) {
            u64 page = (u64)i * 8ULL + bit;
            if (page >= PMM_PAGE_COUNT) break;
            if (!(bitmap[i] & (1U << bit))) {
                bitmap[i] |= (u8)(1U << bit);
                allocated_bitmap[i] |= (u8)(1U << bit);
                if (free_page_count) free_page_count--;
                last_free = i;
                return PMM_BITMAP_BASE + page * 4096ULL;
            }
        }
    }

    for (u32 i = 0; i < last_free && i < BITMAP_SIZE; i++) {
        if (bitmap[i] == 0xFF) continue;

        for (u8 bit = 0; bit < 8; bit++) {
            u64 page = (u64)i * 8ULL + bit;
            if (page >= PMM_PAGE_COUNT) break;
            if (!(bitmap[i] & (1U << bit))) {
                bitmap[i] |= (u8)(1U << bit);
                allocated_bitmap[i] |= (u8)(1U << bit);
                if (free_page_count) free_page_count--;
                last_free = i;
                return PMM_BITMAP_BASE + page * 4096ULL;
            }
        }
    }

    return 0;
}

void pmm_free_page(u64 addr) {
    if (addr < PMM_BITMAP_BASE || addr >= VMM_PHYSICAL_LIMIT ||
        (addr & 4095ULL)) return;

    u64 page = (addr - PMM_BITMAP_BASE) / 4096ULL;
    if (page >= PMM_PAGE_COUNT) return;

    u32 byte = (u32)(page / 8ULL);
    u8 bit = (u8)(page % 8ULL);
    if (!(allocated_bitmap[byte] & (1U << bit))) return; /* reserved or already freed */

    allocated_bitmap[byte] &= (u8)~(1U << bit);
    if (bitmap[byte] & (1U << bit)) {
        bitmap[byte] &= (u8)~(1U << bit);
        free_page_count++;
    }
    if (byte < last_free) last_free = byte;
}

u32 pmm_used_pages(void) {
    /* Only count frames inside firmware-approved managed RAM. PMM_PAGE_COUNT
     * spans the allocator's 64 GiB address window, including holes and
     * unavailable physical ranges; subtracting free pages from it falsely
     * reports every unmanaged frame as occupied RAM. */
    return managed_page_count >= free_page_count
        ? managed_page_count - free_page_count
        : 0;
}

u32 pmm_free_pages(void) {
    /* The cached count is updated with each bitmap transition so live desktop
     * diagnostics remain O(1), not a scan of the entire 64 GiB page bitmap. */
    return free_page_count;
}


u32 pmm_total_pages(void) {
    return managed_page_count;
}
