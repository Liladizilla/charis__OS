#ifndef PMM_H
#define PMM_H

#include <kernel/types.h>
#include <kernel/multiboot.h>

void pmm_init(multiboot_info_t* info);
u64 pmm_alloc_page(void);
void pmm_free_page(u64 addr);

/* Mark [addr, addr+len) as in use so pmm_alloc_page() will not hand it out.
 * Used for regions the firmware does not list as unavailable, notably the
 * framebuffer. */
void pmm_reserve_range(u64 addr, u64 len);

u32 pmm_used_pages(void);
u32 pmm_free_pages(void);

#endif