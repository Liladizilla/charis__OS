/* bootmem.c - Multiboot2 boot information parsing
 *
 * The kernel does not trust the fixed-offset fields of the multiboot info
 * header. This build boots on GRUB 2.1, whose header reports a tags_addr of
 * 21 (unaligned, and not where the tags actually start) and an mmap_addr /
 * mmap_length pair that point into unrelated data. Trusting those fields
 * gave the physical memory manager a garbage memory map and a bogus entry
 * count, and the walk ran off into invalid memory.
 *
 * Instead the tag list is located by validating it: walk from a candidate
 * offset and accept the first one whose tags form a coherent sequence that
 * terminates with an END tag inside the structure. Everything the kernel
 * needs is then read from the tags themselves, which are self-describing:
 *
 *   type 6  memory map, entries inline
 *   type 8  framebuffer description
 *   type 1  kernel command line
 *   type 2  boot loader name
 */

#include <kernel/memory.h>
#include <kernel/bootmem.h>
#include <kernel/vga.h>
#include <kernel/printf.h>

/* Resolved once, during the single memory_init() pass. */
bootmem_display_t boot_info = {0};

/* Memory map, copied out of tag 6. A fixed slot keeps it addressable after
 * the parsing function returns. */
#define BOOTMEM_MAX_REGIONS 48
static mem_map_entry_t g_bootmem_regions[BOOTMEM_MAX_REGIONS];
static u32 g_bootmem_region_count = 0;

static u32 read_le32(u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 read_le64(u8* p) {
    return (u64)read_le32(p) | ((u64)read_le32(p + 4) << 32);
}

/*
 * Try to walk the tag list starting at `off`.
 *
 * Returns true when the sequence is coherent: every tag is at least 8 bytes,
 * no tag runs past the end of the structure, the walk makes forward progress,
 * and it finishes on an END tag. Anything else is rejected and the caller
 * tries the next candidate.
 */
static bool tags_walk_ok(multiboot_info_t* info, u32 off) {
    if (off < 8 || off >= info->total_size) return false;

    u8* base = (u8*)info;
    u32 budget = info->total_size - off;
    u32 used = 0;
    u32 steps = 0;

    while (steps++ < 256) {
        u8* t = base + off + used;
        u32 type = read_le32(t);
        u32 size = read_le32(t + 4);

        if (type == MULTIBOOT_TAG_END) return true;
        if (size < 8) return false;
        if (size > budget - used) return false;

        used += (size + 7) & ~7u;
        if (used >= budget) return false;
    }
    return false;
}

/* Read the memory map out of tag 6. */
static void bootmem_read_mmap(multiboot_info_t* info, u32 off) {
    g_bootmem_region_count = 0;

    u8* base = (u8*)info;
    u32 used = 0;
    u32 budget = info->total_size - off;

    while (used + 8 <= budget) {
        u8* t = base + off + used;
        u32 type = read_le32(t);
        u32 size = read_le32(t + 4);

        if (type == MULTIBOOT_TAG_END) break;

        if (type == MULTIBOOT_TAG_MMAP && size >= 16) {
            u32 entry_size = read_le32(t + 8);
            if (entry_size < 24) break;

            u32 entries_off = 16;
            while (entries_off + entry_size <= size &&
                   g_bootmem_region_count < BOOTMEM_MAX_REGIONS) {
                u8* e = t + entries_off;
                mem_map_entry_t* r = &g_bootmem_regions[g_bootmem_region_count++];
                r->base  = read_le64(e);
                r->length = read_le64(e + 8);
                r->type  = read_le32(e + 16);
                r->reserved = 0;
                entries_off += entry_size;
            }
            break;
        }

        if (size < 8) break;
        used += (size + 7) & ~7u;
    }
}

void bootmem_parse_multiboot(multiboot_info_t* info) {
    boot_info.has_framebuffer = false;
    boot_info.cmdline = 0;
    boot_info.boot_loader = 0;
    g_bootmem_region_count = 0;

    if (!info || info->total_size < 32) {
        vga_puts("Boot: multiboot info missing or implausibly small\n");
        return;
    }

    /* Locate a tag list that actually validates. */
    u32 tags_off = 0;
    for (u32 cand = 8; cand < 64 && !tags_off; cand += 4) {
        if (tags_walk_ok(info, cand)) tags_off = cand;
    }

    if (!tags_off) {
        vga_puts("Boot: no valid multiboot tag list found\n");
        return;
    }

    vga_printf("Boot: tag list at offset %u of %u bytes\n", tags_off, info->total_size);
    boot_info.tags_offset = tags_off;

    /* Now that the list is known-good, pull out what we need. */
    u8* base = (u8*)info;
    u32 used = 0;
    u32 budget = info->total_size - tags_off;

    while (used + 8 <= budget) {
        u8* t = base + tags_off + used;
        u32 type = read_le32(t);
        u32 size = read_le32(t + 4);

        if (type == MULTIBOOT_TAG_END) break;

        switch (type) {
        case MULTIBOOT_TAG_CMDLINE:
        case MULTIBOOT_TAG_BOOT_LOADER_NAME:
            if (size > 8) {
                if (type == MULTIBOOT_TAG_CMDLINE)
                    boot_info.cmdline = (const char*)(t + 8);
                else
                    boot_info.boot_loader = (const char*)(t + 8);
            }
            break;

        case MULTIBOOT_TAG_MMAP:
            bootmem_read_mmap(info, tags_off);
            break;

        case MULTIBOOT_TAG_FRAMEBUFFER: {
            if (size < 32) break;
            boot_info.framebuffer_addr   = read_le64(t + 8);
            boot_info.framebuffer_pitch  = read_le32(t + 16);
            boot_info.framebuffer_width  = read_le32(t + 20);
            boot_info.framebuffer_height = read_le32(t + 24);
            boot_info.framebuffer_bpp    = t[28];
            boot_info.framebuffer_type   = t[29];

            /* The colour-position fields live at offsets 32, 33 and 34, so
             * they are only present when the tag is at least 35 bytes long.
             * Firmware does emit shorter tags, and reading past one produced a
             * blue position copied from whatever followed -- which silently
             * overwrote green. Default to the usual XRGB8888 layout. */
            if (boot_info.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB &&
                size >= 35) {
                boot_info.framebuffer_red   = t[32];
                boot_info.framebuffer_green = t[33];
                boot_info.framebuffer_blue  = t[34];
            } else {
                boot_info.framebuffer_red   = 16;
                boot_info.framebuffer_green = 8;
                boot_info.framebuffer_blue  = 0;
            }
            boot_info.has_framebuffer = true;
            break;
        }

        default:
            break;
        }

        if (size < 8) break;
        used += (size + 7) & ~7u;
    }

    boot_info.region_count = g_bootmem_region_count;

    vga_printf("Boot: %u memory regions from the mmap tag\n", g_bootmem_region_count);
    if (g_bootmem_region_count) {
        mem_map_entry_t* r = &g_bootmem_regions[0];
        vga_printf("Boot: first region base=%p len=%p type=%u\n",
                   (void*)(uintptr_t)r->base, (void*)(uintptr_t)r->length, r->type);
    }

    if (boot_info.has_framebuffer) {
        vga_printf("Boot: framebuffer %ux%u %u bpp at %p (pitch %u) type=%u\n",
                   boot_info.framebuffer_width,
                   boot_info.framebuffer_height,
                   boot_info.framebuffer_bpp,
                   (void*)(uintptr_t)boot_info.framebuffer_addr,
                   boot_info.framebuffer_pitch,
                   boot_info.framebuffer_type);
    } else {
        vga_puts("Boot: no framebuffer tag\n");
    }
}

const mem_map_entry_t* bootmem_regions(u32* count) {
    if (count) *count = g_bootmem_region_count;
    return g_bootmem_regions;
}
