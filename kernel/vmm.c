#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/memory.h>

// Page table entry flags
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_NX          (1ULL << 63)

// Page table levels
#define PML4_INDEX(virt) (((virt) >> 39) & 0x1FF)
#define PDPT_INDEX(virt) (((virt) >> 30) & 0x1FF)
#define PD_INDEX(virt)   (((virt) >> 21) & 0x1FF)
#define PT_INDEX(virt)   (((virt) >> 12) & 0x1FF)

// Get current CR3
static u64 get_cr3(void) {
    u64 cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Invalidate TLB entry
static void invlpg(u64 addr) {
    asm volatile("invlpg (%0)" : : "r"(addr));
}

// Phase 1 safety: do not use (phys | higher-half) mapping for page-table pages.
// This implementation assumes the physical memory is identity/direct mapped during early boot.
static inline u64* phys_to_virt(u64 phys) {
    return (u64*)(phys);
}

// get or create a lower-level page table using a parent_table[idx] entry.
// flags is currently only used for the present entry's writable bit.
static bool get_or_create_table(u64* parent_table, u16 idx, u64 flags) {
    u64 entry = parent_table[idx];
    if (entry & PTE_PRESENT) {
        return true;
    }

    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    // Zero the newly allocated table page
    kmemset(phys_to_virt(phys), 0, 4096);

    // Create pointer entry (table physical address + flags)
    parent_table[idx] = (phys & ~0xFFFULL) | PTE_PRESENT | (flags & PTE_USER) | PTE_WRITABLE;
    return true;
}


bool vmm_map_page(u64 virt, u64 phys, u64 flags) {
    u64* pml4 = (u64*)get_cr3();

    // PML4 -> PDPT
    u16 i1 = PML4_INDEX(virt);
    if (!get_or_create_table(pml4, i1, 0)) return false;
    u64 pml4e = pml4[i1]; // reload after insertion
    u64* pdpt = phys_to_virt(pml4e & ~0xFFFULL);

    // PDPT -> PD
    u16 i2 = PDPT_INDEX(virt);
    if (!get_or_create_table(pdpt, i2, 0)) return false;
    u64 pdpte = pdpt[i2]; // reload
    u64* pd = phys_to_virt(pdpte & ~0xFFFULL);

    // PD -> PT
    u16 i3 = PD_INDEX(virt);
    if (!get_or_create_table(pd, i3, 0)) return false;
    u64 pde = pd[i3]; // reload
    u64* pt = phys_to_virt(pde & ~0xFFFULL);

    // PT -> PTE
    pt[PT_INDEX(virt)] = (phys & ~0xFFFULL) | PTE_PRESENT | (flags & (PTE_WRITABLE | PTE_USER | PTE_NX));

    invlpg(virt);
    return true;
}

void vmm_unmap_page(u64 virt) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return;

    u64* pdpt = (u64*)(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return;

    u64* pd = (u64*)(pdpte & ~0xFFFULL);
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return;

    if (pde & (1ULL << 7)) {
        // 2MB huge page: unsupported unmap in this Phase 1 implementation
        return;
    }

    u64* pt = (u64*)(pde & ~0xFFFULL);
    pt[PT_INDEX(virt)] = 0;

    invlpg(virt);
}

u64 vmm_get_phys(u64 virt) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return 0;

    u64* pdpt = (u64*)(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return 0;

    u64* pd = (u64*)(pdpte & ~0xFFFULL);
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return 0;

    // huge page (2MB)
    if (pde & (1ULL << 7)) {
        return (pde & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
    }

    u64* pt = (u64*)(pde & ~0xFFFULL);
    u64 pte = pt[PT_INDEX(virt)];
    if (!(pte & PTE_PRESENT)) return 0;

    return (pte & ~0xFFFULL) | (virt & 0xFFFULL);
}

void vmm_init(void) {
    vga_puts("VMM initialized\n");
}

