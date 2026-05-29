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

// Physical-to-virtual mapping for page-table pages.
// Boot.asm sets up identity mapping for first 1GB, so physical addresses equal virtual addresses.
// This works for page tables allocated in low memory.
static inline u64* phys_to_virt_pt(u64 phys) {
    return (u64*)(phys);
}

// Get current CR3
static u64 get_cr3(void) {
    u64 cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// Full TLB flush via CR3 reload
static void flush_tlb(void) {
    u64 cr3 = get_cr3();
    asm volatile("mov %0, %%cr3" : : "r"(cr3));
}

// Get or create a lower-level page table
static bool get_or_create_table(u64* parent_table, u16 idx, u64 flags) {
    u64 entry = parent_table[idx];
    if (entry & PTE_PRESENT) {
        return true;
    }

    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    kmemset(phys_to_virt_pt(phys), 0, PAGE_SIZE);

    parent_table[idx] = (phys & ~0xFFFULL) | PTE_PRESENT | (flags & PTE_USER) | PTE_WRITABLE;
    return true;
}


bool vmm_map_page(u64 virt, u64 phys, u64 flags) {
    u64* pml4 = (u64*)get_cr3();

    u16 i1 = PML4_INDEX(virt);
    if (!get_or_create_table(pml4, i1, 0)) return false;
    u64 pml4e = pml4[i1];
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);

    u16 i2 = PDPT_INDEX(virt);
    if (!get_or_create_table(pdpt, i2, 0)) return false;
    u64 pdpte = pdpt[i2];
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);

    u16 i3 = PD_INDEX(virt);
    if (!get_or_create_table(pd, i3, 0)) return false;
    u64 pde = pd[i3];
    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);

    pt[PT_INDEX(virt)] = (phys & ~0xFFFULL) | PTE_PRESENT | (flags & (PTE_WRITABLE | PTE_USER | PTE_NX));

    flush_tlb();
    return true;
}

void vmm_unmap_page(u64 virt) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return;

    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return;

    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return;

    if (pde & (1ULL << 7)) {
        return;
    }

    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    pt[PT_INDEX(virt)] = 0;

    flush_tlb();
}

u64 vmm_get_phys(u64 virt) {
    u64* pml4 = (u64*)get_cr3();
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return 0;

    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return 0;

    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return 0;

    if (pde & (1ULL << 7)) {
        return (pde & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
    }

    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    u64 pte = pt[PT_INDEX(virt)];
    if (!(pte & PTE_PRESENT)) return 0;

    return (pte & ~0xFFFULL) | (virt & 0xFFFULL);
}

void vmm_init(void) {
    vga_puts("VMM initialized\n");
}

// Walk the page table tree and return the PTE for a virtual address.
// If create=true, allocate missing intermediate tables.
pte_t* vmm_walk(pml4_t* pml4, u64 vaddr, bool create) {
    u64* pml4_table = (u64*)pml4;
    
    u16 i1 = PML4_INDEX(vaddr);
    u64 pml4e = pml4_table[i1];
    if (!(pml4e & PTE_PRESENT)) {
        if (!create) return NULL;
        u64 phys = pmm_alloc_page();
        if (!phys) return NULL;
        kmemset(phys_to_virt_pt(phys), 0, PAGE_SIZE);
        pml4_table[i1] = (phys & ~0xFFFULL) | PTE_PRESENT | PTE_WRITABLE;
        pml4e = pml4_table[i1];
    }
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);

    u16 i2 = PDPT_INDEX(vaddr);
    u64 pdpte = pdpt[i2];
    if (!(pdpte & PTE_PRESENT)) {
        if (!create) return NULL;
        u64 phys = pmm_alloc_page();
        if (!phys) return NULL;
        kmemset(phys_to_virt_pt(phys), 0, PAGE_SIZE);
        pdpt[i2] = (phys & ~0xFFFULL) | PTE_PRESENT | PTE_WRITABLE;
        pdpte = pdpt[i2];
    }
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);

    u16 i3 = PD_INDEX(vaddr);
    u64 pde = pd[i3];
    if (!(pde & PTE_PRESENT)) {
        if (!create) return NULL;
        u64 phys = pmm_alloc_page();
        if (!phys) return NULL;
        kmemset(phys_to_virt_pt(phys), 0, PAGE_SIZE);
        pd[i3] = (phys & ~0xFFFULL) | PTE_PRESENT | PTE_WRITABLE;
        pde = pd[i3];
    }
    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);

    return &pt[PT_INDEX(vaddr)];
}

// Create a new empty page table (for new processes).
pml4_t* vmm_create_address_space(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return NULL;
    
    u64* pml4 = phys_to_virt_pt(phys);
    kmemset(pml4, 0, PAGE_SIZE);
    
    return (pml4_t*)phys;
}

// Copy kernel mappings into a new page table (needed for every new process).
// Kernel is in the first 1GB identity-mapped region, so copy PML4[0].
void vmm_copy_kernel_mappings(pml4_t* dst, pml4_t* src) {
    (void)src;
    u64* src_pml4 = (u64*)get_cr3();
    
    // Copy PML4 entry 0 (identity-mapped low memory)
    ((u64*)dst)[0] = src_pml4[0];
}

// Switch the CPU to use a given page table.
void vmm_switch(pml4_t* pml4) {
    asm volatile("mov %0, %%cr3" : : "r"(pml4));
}

// Map a page into a specific address space (not current)
bool vmm_map_page_pml4(pml4_t* pml4, u64 virt, u64 phys, u64 flags) {
    u64* pml4_table = (u64*)pml4;
    
    u16 i1 = PML4_INDEX(virt);
    if (!get_or_create_table(pml4_table, i1, 0)) return false;
    u64 pml4e = pml4_table[i1];
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);

    u16 i2 = PDPT_INDEX(virt);
    if (!get_or_create_table(pdpt, i2, 0)) return false;
    u64 pdpte = pdpt[i2];
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);

    u16 i3 = PD_INDEX(virt);
    if (!get_or_create_table(pd, i3, 0)) return false;
    u64 pde = pd[i3];
    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);

    pt[PT_INDEX(virt)] = (phys & ~0xFFFULL) | PTE_PRESENT | (flags & (PTE_WRITABLE | PTE_USER | PTE_NX));

    return true;
}

