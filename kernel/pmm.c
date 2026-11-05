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

/* The boot page tables identity-map [0, 4 GiB); never allocate outside
 * that directly addressable physical window until a larger direct map exists. */
#define PMM_MAX_PHYS 0x100000000ULL

static void pmm_mark_used(u64 addr) {
    if (addr < bitmap_base || addr >= PMM_MAX_PHYS) return;
    u32 page = (u32)((addr - bitmap_base) / 4096);
    bitmap[page / 8] |= (u8)(1U << (page % 8));
}

static void pmm_mark_free(u64 addr) {
    if (addr < bitmap_base || addr >= PMM_MAX_PHYS) return;
    u32 page = (u32)((addr - bitmap_base) / 4096);
    bitmap[page / 8] &= (u8)~(1U << (page % 8));
}

static u64 pmm_align_up_page(u64 value) {
    if (value > UINT64_MAX - 4095ULL) return UINT64_MAX;
    return (value + 4095ULL) & ~4095ULL;
}

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

    /*
     * Default every managed page to RESERVED. Only pages wholly contained in
     * an explicit Multiboot AVAILABLE region are opened for allocation. This
     * prevents holes and unreported physical ranges from being mistaken for
     * RAM merely because no reserved entry described them.
     */
    kmemset(bitmap, 0xFF, BITMAP_SIZE);
    for (u32 i = 0; i < memory_map_count; i++) {
        if (memory_map[i].type != MULTIBOOT_MEMORY_AVAILABLE) continue;
        u64 region_end = memory_map[i].base + memory_map[i].length;
        if (region_end < memory_map[i].base) continue;

        u64 start = pmm_align_up_page(memory_map[i].base);
        if (start < bitmap_base) start = bitmap_base;
        u64 end = region_end & ~4095ULL;
        if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;

        for (u64 addr = start; addr < end; addr += 4096) {
            pmm_mark_free(addr);
        }
    }

    /* Re-apply non-available entries after the clear pass so a malformed or
     * overlapping AVAILABLE range can never override a firmware reservation. */
    for (u32 i = 0; i < memory_map_count; i++) {
        if (memory_map[i].type == MULTIBOOT_MEMORY_AVAILABLE) continue;
        u64 region_end = memory_map[i].base + memory_map[i].length;
        if (region_end < memory_map[i].base) continue;

        u64 start = memory_map[i].base & ~4095ULL;
        u64 end = pmm_align_up_page(region_end);
        if (start < bitmap_base) start = bitmap_base;
        if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;

        for (u64 addr = start; addr < end; addr += 4096) {
            pmm_mark_used(addr);
        }
    }

    /* Mark the kernel image and the still-referenced Multiboot tag block used. */
    u64 kernel_start = 0x100000;
    u64 kernel_end = (u64)_kernel_end;
    for (u64 addr = kernel_start; addr < kernel_end; addr += 4096) {
        pmm_mark_used(addr);
    }
    if (info && info->total_size) {
        pmm_reserve_range((u64)(uintptr_t)info, info->total_size);
    }

    vga_printf("PMM: %u regions, %u pages used, %u free\n",
               memory_map_count, pmm_used_pages(), pmm_free_pages());
}

void pmm_reserve_range(u64 addr, u64 len) {
    if (!len || addr >= PMM_MAX_PHYS) return;
    u64 end = addr + len;
    if (end < addr) end = PMM_MAX_PHYS;
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;

    u64 start = addr & ~4095ULL;
    end = pmm_align_up_page(end);
    if (end > PMM_MAX_PHYS) end = PMM_MAX_PHYS;
    if (start < bitmap_base) start = bitmap_base;

    for (u64 a = start; a < end; a += 4096) {
        pmm_mark_used(a);
    }
}

u64 pmm_alloc_page(void) {
    /* The final bitmap byte includes the 1 MiB tail beyond 4 GiB; those bits
     * are permanently reserved by pmm_init(), but enforce the boundary here
     * too so corrupted bookkeeping can never return an unmapped page. */
    const u32 allocatable_pages = (u32)((PMM_MAX_PHYS - bitmap_base) / 4096ULL);
    u32 i;
    for (i = last_free; i < allocatable_pages / 8 + 1 && i < BITMAP_SIZE; i++) {
        if (bitmap[i] != 0xFF) {
            u8 bit;
            for (bit = 0; bit < 8; bit++) {
                if (!(bitmap[i] & (1 << bit))) {
                    bitmap[i] |= (1 << bit);
                    last_free = i;
                    u64 addr = bitmap_base + (u64)(i * 8 + bit) * 4096ULL;
                    if (addr >= PMM_MAX_PHYS) {
                        bitmap[i] |= (u8)(1U << bit);
                        continue;
                    }
                    return addr;
                }
            }
        }
    }
    // wrap around
    for (i = 0; i < last_free && i < allocatable_pages / 8 + 1; i++) {
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
    if (addr < bitmap_base || addr >= PMM_MAX_PHYS) return; // invalid/outside direct map
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