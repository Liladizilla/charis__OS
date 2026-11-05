#ifndef VMM_H
#define VMM_H

#include <kernel/types.h>

typedef u64 pte_t;
typedef u64 pml4_t;

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
/* Present in RUN_VMM_TESTS builds; exercises page-table and permission paths. */
void vmm_run_tests(void);

pml4_t* vmm_create_address_space(void);
bool vmm_copy_kernel_mappings(pml4_t* dst, pml4_t* src);
void vmm_switch(pml4_t* pml4);

#endif