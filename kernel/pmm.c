#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/multiboot.h>
#include <kernel/printf.h>
#include <kernel/bootmem.h>

/*
 * The boot assembly identity-maps physical memory only below 1 GiB.
 * Until a direct-map window exists, the allocator must never return a page
 * above that limit: page-table code dereferences physical addresses directly.
 */
#define PMM_BASE       0x00100000ULL
#define PMM_LIMIT      0x40000000ULL
#define PMM_PAGE_COUNT ((PMM_LIMIT - PMM_BASE) / 4096ULL)
#define BITMAP_SIZE    ((PMM_PAGE_COUNT + 7ULL) / 8ULL)

static const mem_map_entry_t* memory_map;
static u32 memory_map_count;
static u8 bitmap[BITMAP_SIZE];
static u32 last_free = 0;

extern char _kernel_end[];

static void pmm_mark_range(u64 start, u64 end, bool used) {
    if (end <= start || end <= PMM_BASE || start >= PMM_LIMIT) return;
    if (start < PMM_BASE) start = PMM_BASE;
    if (end > PMM_LIMIT) end = PMM_LIMIT;

    /* Only whole pages fully contained in an available region are usable. */
    start = ALIGN_UP(start, 4096ULL);
    end = ALIGN_DOWN(end, 4096ULL);
    for (u64 addr = start; addr < end; addr += 4096ULL) {
        u64 page = (addr - PMM_BASE) / 4096ULL;
        u8 mask = (u8)(1U << (page & 7U));
        if (used) bitmap[page >> 3] |= mask;
        else bitmap[page >> 3] &= (u8)~mask;
    }
}

void pmm_init(multiboot_info_t* info) {
    (void)info;
    u32 count = 0;
    const mem_map_entry_t* regions = bootmem_regions(&count);
    memory_map = regions;
    memory_map_count = count;

    /* Fail closed: RAM is reserved until the firmware explicitly marks it usable. */
    kmemset(bitmap, 0xFF, sizeof(bitmap));
    last_free = 0;

    for (u32 i = 0; i < memory_map_count; i++) {
        if (memory_map[i].type != MULTIBOOT_MEMORY_AVAILABLE) continue;
        u64 start = memory_map[i].base;
        u64 length = memory_map[i].length;
        if (length > (~0ULL - start)) continue;
        pmm_mark_range(start, start + length, false);
    }

    /* Kernel image and early boot structures must never be handed to callers. */
    pmm_mark_range(0x00100000ULL, ALIGN_UP((u64)_kernel_end, 4096ULL), true);

    vga_printf("PMM: %u regions, managed below 1 GiB, %u pages used, %u free\n",
               memory_map_count, pmm_used_pages(), pmm_free_pages());
}

void pmm_reserve_range(u64 addr, u64 len) {
    if (!len || addr >= PMM_LIMIT) return;
    u64 end = (len > PMM_LIMIT - addr) ? PMM_LIMIT : addr + len;
    if (end < PMM_LIMIT) end = ALIGN_UP(end, 4096ULL);
    pmm_mark_range(ALIGN_DOWN(addr, 4096ULL), end, true);
}

u64 pmm_alloc_page(void) {
    for (u32 i = last_free; i < BITMAP_SIZE; i++) {
        if (bitmap[i] == 0xFF) continue;
        for (u8 bit = 0; bit < 8; bit++) {
            u32 page = i * 8U + bit;
            if ((u64)page >= PMM_PAGE_COUNT) break;
            u8 mask = (u8)(1U << bit);
            if (!(bitmap[i] & mask)) {
                bitmap[i] |= mask;
                last_free = i;
                u64 address = PMM_BASE + (u64)page * 4096ULL;
                /* Zero-on-allocation prevents stale data crossing process boundaries. */
                kmemset((void*)(uintptr_t)address, 0, 4096);
                return address;
            }
        }
    }

    for (u32 i = 0; i < last_free; i++) {
        if (bitmap[i] == 0xFF) continue;
        for (u8 bit = 0; bit < 8; bit++) {
            u32 page = i * 8U + bit;
            if ((u64)page >= PMM_PAGE_COUNT) break;
            u8 mask = (u8)(1U << bit);
            if (!(bitmap[i] & mask)) {
                bitmap[i] |= mask;
                last_free = i;
                u64 address = PMM_BASE + (u64)page * 4096ULL;
                kmemset((void*)(uintptr_t)address, 0, 4096);
                return address;
            }
        }
    }
    return 0;
}

void pmm_free_page(u64 addr) {
    /* Reject unaligned, out-of-range and double frees. */
    if ((addr & 4095ULL) || addr < PMM_BASE || addr >= PMM_LIMIT) return;
    u64 page = (addr - PMM_BASE) / 4096ULL;
    u32 byte = (u32)(page >> 3);
    u8 bit = (u8)(page & 7U);
    u8 mask = (u8)(1U << bit);
    if (!(bitmap[byte] & mask)) return;
    bitmap[byte] |= mask;
    if (byte < last_free) last_free = byte;
}

u32 pmm_used_pages(void) {
    u32 count = 0;
    for (u32 i = 0; i < BITMAP_SIZE; i++) {
        u8 value = bitmap[i];
        for (u8 bit = 0; bit < 8; bit++) {
            if (value & (1U << bit)) count++;
        }
    }
    return count;
}

u32 pmm_free_pages(void) {
    return (u32)PMM_PAGE_COUNT - pmm_used_pages();
}
