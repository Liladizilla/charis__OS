#ifndef VMM_H
#define VMM_H

#include <kernel/types.h>

bool vmm_map_page(u64 virt, u64 phys, u64 flags);
void vmm_unmap_page(u64 virt);
u64 vmm_get_phys(u64 virt);
void vmm_init(void);

#endif