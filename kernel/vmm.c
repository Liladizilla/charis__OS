#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>

// Page table entry flags
#define PTE_PRESENT     (1 << 0)
#define PTE_WRITABLE    (1 << 1)
#define PTE_USER        (1 << 2)
#define PTE_PWT         (1 << 3)
#define PTE_PCD         (1 << 4)
#define PTE_ACCESSED    (1 << 5)
#define PTE_DIRTY       (1 << 6)
#define PTE_PS          (1 << 7)  // Page size
#define PTE_GLOBAL      (1 << 8)
#define PTE_NX          (1 << 63) // No execute (if supported)

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

// Allocate a page table if needed
static u64* alloc_pt(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return NULL;
    // Map temporarily? For now, assume identity mapped
    return (u64*)(phys | 0xFFFF800000000000); // Higher half
}

// Map a virtual page to physical
bool vmm_map_page(u64 virt, u64 phys, u64 flags) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];

    if (!(pml4e & PTE_PRESENT)) {
        u64* pdpt = alloc_pt();
        if (!pdpt) return false;
        pml4[PML4_INDEX(virt)] = (u64)pdpt | PTE_PRESENT | PTE_WRITABLE;
    }

    u64* pdpt = (u64*)(pml4e & ~0xFFF);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];

    if (!(pdpte & PTE_PRESENT)) {
        u64* pd = alloc_pt();
        if (!pd) return false;
        pdpt[PDPT_INDEX(virt)] = (u64)pd | PTE_PRESENT | PTE_WRITABLE;
    }

    u64* pd = (u64*)(pdpte & ~0xFFF);
    u64 pde = pd[PD_INDEX(virt)];

    if (!(pde & PTE_PRESENT)) {
        u64* pt = alloc_pt();
        if (!pt) return false;
        pd[PD_INDEX(virt)] = (u64)pt | PTE_PRESENT | PTE_WRITABLE;
    }

    u64* pt = (u64*)(pde & ~0xFFF);
    pt[PT_INDEX(virt)] = phys | flags | PTE_PRESENT;

    invlpg(virt);
    return true;
}

// Unmap a page
void vmm_unmap_page(u64 virt) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return;

    u64* pdpt = (u64*)(pml4e & ~0xFFF);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return;

    u64* pd = (u64*)(pdpte & ~0xFFF);
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return;

    u64* pt = (u64*)(pde & ~0xFFF);
    pt[PT_INDEX(virt)] = 0;

    invlpg(virt);
}

// Get physical address for virtual
u64 vmm_get_phys(u64 virt) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return 0;

    u64* pdpt = (u64*)(pml4e & ~0xFFF);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return 0;

    u64* pd = (u64*)(pdpte & ~0xFFF);
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return 0;

    if (pde & PTE_PS) {
        // 2MB page
        return (pde & ~0x1FFFFF) | (virt & 0x1FFFFF);
    }

    u64* pt = (u64*)(pde & ~0xFFF);
    u64 pte = pt[PT_INDEX(virt)];
    if (!(pte & PTE_PRESENT)) return 0;

    return (pte & ~0xFFF) | (virt & 0xFFF);
}

void vmm_init(void) {
    // Identity map first 1GB (already done in boot)
    // Higher half kernel already identity mapped
    vga_puts("VMM initialized\n");
}