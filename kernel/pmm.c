#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/multiboot.h>
#include <kernel/printf.h>
#include <kernel/bootmem.h>

// Memory map from Multiboot
static const mem_map_entry_t* memory_map;
static u32 memory_map_count;

// Bitmap for 4GB (assuming 4KB pages, 128KB bitmap)
#define BITMAP_SIZE (1024 * 1024 / 8) // 128KB for 4GB
static u8 bitmap[BITMAP_SIZE];
static u64 bitmap_base = 0x100000;
static u32 last_free = 0; // next-fit cursor for speed

extern char _kernel_end[];

void pmm_init(multiboot_info_t* info) {
    (void)info;

    /* The map comes from the mmap tag, recovered by bootmem_parse_multiboot().
     * The info header's mmap_addr/mmap_length fields are not usable on this
     * bootloader: they point at unrelated data, which previously produced a
     * nine-million-entry "map" and a walk off into invalid memory. */
    u32 count = 0;
    const mem_map_entry_t* regions = bootmem_regions(&count);
    memory_map = regions;
    memory_map_count = count;

    /* Clear bitmap */
    kmemset(bitmap, 0, BITMAP_SIZE);

    /* Mark the kernel image in use. */
    u64 kernel_start = 0x100000;
    u64 kernel_end = (u64)_kernel_end;
    for (u64 addr = kernel_start; addr < kernel_end; addr += 4096) {
        u32 page = (addr - bitmap_base) / 4096;
        if (page < BITMAP_SIZE * 8) bitmap[page / 8] |= (1 << (page % 8));
    }

    /* Everything the firmware did not mark available: its own reservations,
     * ACPI tables, the framebuffer, option ROMs and so on. */
    for (u32 i = 0; i < memory_map_count; i++) {
        if (memory_map[i].type == MULTIBOOT_MEMORY_AVAILABLE) continue;
        u64 start = memory_map[i].base;
        u64 end = start + memory_map[i].length;
        if (end < start) continue;
        for (u64 addr = start; addr < end; addr += 4096) {
            if (addr < bitmap_base) continue;
            u32 page = (addr - bitmap_base) / 4096;
            if (page >= BITMAP_SIZE * 8) break;
            bitmap[page / 8] |= (1 << (page % 8));
        }
    }

    vga_printf("PMM: %u regions, %u pages used, %u free\n",
               memory_map_count, pmm_used_pages(), pmm_free_pages());
}

void pmm_reserve_range(u64 addr, u64 len) {
    u64 start = addr & ~(u64)4095;
    u64 end   = (addr + len + 4095) & ~(u64)4095;

    for (u64 a = start; a < end; a += 4096) {
        if (a < bitmap_base) continue;
        u64 page = (a - bitmap_base) / 4096;
        if (page >= (u64)BITMAP_SIZE * 8) break;
        bitmap[page / 8] |= (1 << (page % 8));
    }
}

u64 pmm_alloc_page(void) {
    u32 i;
    for (i = last_free; i < BITMAP_SIZE; i++) {
        if (bitmap[i] != 0xFF) {
            u8 bit;
            for (bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    last_free = i;
                    return bitmap_base + (i * 8 + bit) * 4096;
                }
            }
        }
    }
    // wrap around
    for (i = 0; i < last_free; i++) {
        if (bitmap[i] != 0xFF) {
            u8 bit;
            for (bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    last_free = i;
                    return bitmap_base + (i * 8 + bit) * 4096;
                }
            }
        }
    }
    return 0;
}

void pmm_free_page(u64 addr) {
    if (addr < bitmap_base) return; // invalid
    u32 page = (addr - bitmap_base) / 4096;
    if (page >= BITMAP_SIZE * 8) return;
    u32 byte = page / 8;
    u8 bit = page % 8;
    if ((bitmap[byte] & (1 << bit)) == 0) return; // double-free protection
    bitmap[byte] &= ~(1 << bit);
    if (byte < last_free) last_free = byte;
}

u32 pmm_used_pages(void) {
    u32 count = 0;
    for (u32 i = 0; i < BITMAP_SIZE; i++) {
        for (u8 bit = 0; bit < 8; bit++) {
            if (bitmap[i] & (1 << bit)) count++;
        }
    }
    return count;
}

u32 pmm_free_pages(void) {
    return (u32)(BITMAP_SIZE * 8) - pmm_used_pages();
}