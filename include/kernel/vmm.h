#ifndef VMM_H
#define VMM_H

#include <kernel/types.h>

typedef u64 pte_t;
typedef u64 pml4_t;

/* Physical frames managed by PMM and exposed in a supervisor-only direct map. */
#define VMM_PHYSICAL_LIMIT (64ULL * 1024ULL * 1024ULL * 1024ULL)

#define VMM_FLAG_PRESENT    (1ULL << 0)
#define VMM_FLAG_WRITABLE   (1ULL << 1)
#define VMM_FLAG_USER       (1ULL << 2)
#define VMM_FLAG_NX         (1ULL << 63)

/* Returns a 4 KiB PTE pointer, or NULL for a missing/huge leaf. */
pte_t* vmm_walk(pml4_t* pml4, u64 vaddr, bool create);
/* Huge-page-aware translation with effective permissions from every level. */
bool vmm_translate(pml4_t* pml4, u64 vaddr, u64* physical, u64* flags);
bool vmm_map_page(u64 virt, u64 phys, u64 flags);
bool vmm_map_page_pml4(pml4_t* pml4, u64 virt, u64 phys, u64 flags);
void vmm_unmap_page(u64 virt);
/* Clear a leaf mapping in any address space and return its physical page base. */
u64 vmm_unmap_page_pml4(pml4_t* pml4, u64 virt);
u64 vmm_get_phys(u64 virt);
void vmm_init(void);
/* Optional entry point compiled only for the VMM regression image. */
void vmm_run_tests(void);
/* Convert a physical address to its kernel-only direct-map alias, or NULL. */
void* vmm_phys_to_virt(u64 physical);

pml4_t* vmm_create_address_space(void);
bool vmm_copy_kernel_mappings(pml4_t* dst, pml4_t* src);
void vmm_switch(pml4_t* pml4);

#endif