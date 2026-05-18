#include <kernel/memory.h>

// Phase 1 placeholder: multiboot parsing lives in pmm_init() for now.
// This file exists to support the requested split.

void bootmem_parse_multiboot(multiboot_info_t* info) {
    (void)info;
    // No-op for now; pmm_init() currently consumes the multiboot memory map directly.
}

