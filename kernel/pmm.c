#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>

// Memory map from Multiboot
static multiboot_memory_map_t* memory_map;
static u32 memory_map_count;

// Bitmap for 4GB (assuming 4KB pages, 128KB bitmap)
#define BITMAP_SIZE (1024 * 1024 / 8) // 128KB for 4GB
static u8 bitmap[BITMAP_SIZE];
static u64 bitmap_base = 0x100000; // After 1MB

void pmm_init(multiboot_info_t* info) {
    memory_map = (multiboot_memory_map_t*)info->mmap_addr;
    memory_map_count = info->mmap_length / sizeof(multiboot_memory_map_t);

    // Clear bitmap
    memset(bitmap, 0, BITMAP_SIZE);

    // Mark used areas
    // Kernel (1MB - end)
    u64 kernel_start = 0x100000;
    u64 kernel_end = _kernel_end;
    for (u64 addr = kernel_start; addr < kernel_end; addr += 4096) {
        u32 page = (addr - bitmap_base) / 4096;
        bitmap[page / 8] |= (1 << (page % 8));
    }

    // Modules, framebuffer, etc. from memory map
    for (u32 i = 0; i < memory_map_count; i++) {
        if (memory_map[i].type != MULTIBOOT_MEMORY_AVAILABLE) {
            u64 start = memory_map[i].addr;
            u64 end = start + memory_map[i].len;
            for (u64 addr = start; addr < end; addr += 4096) {
                if (addr >= bitmap_base) {
                    u32 page = (addr - bitmap_base) / 4096;
                    if (page < BITMAP_SIZE * 8) {
                        bitmap[page / 8] |= (1 << (page % 8));
                    }
                }
            }
        }
    }

    vga_puts("PMM initialized\n");
}

u64 pmm_alloc_page(void) {
    for (u32 i = 0; i < BITMAP_SIZE; i++) {
        if (bitmap[i] != 0xFF) {
            for (u8 bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    return bitmap_base + (i * 8 + bit) * 4096;
                }
            }
        }
    }
    return 0; // Out of memory
}

void pmm_free_page(u64 addr) {
    u32 page = (addr - bitmap_base) / 4096;
    bitmap[page / 8] &= ~(1 << (page % 8));
}