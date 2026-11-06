#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/memory.h>
#include <kernel/bootmem.h>

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

/*
 * Static page-table storage permits the direct map to be installed before
 * PMM allocations are used as pointers. All entries are supervisor-only.
 */
#define PHYSMAP_PDPT_ENTRIES (VMM_PHYSICAL_LIMIT >> 30)
static u64 physmap_pdpt[512] __attribute__((aligned(PAGE_SIZE)));
static u64 physmap_pd[PHYSMAP_PDPT_ENTRIES][512] __attribute__((aligned(PAGE_SIZE)));
static u64 physmap_base = 0;
static bool physmap_ready = false;
static u64 kernel_cr3 = 0;

void* vmm_phys_to_virt(u64 physical) {
    if (physical >= VMM_PHYSICAL_LIMIT) return NULL;
    /* Keep the first 2 MiB on the existing identity map. It contains legacy
     * firmware regions as well as bootstrap tables, so do not add a second
     * write-back alias for that mixed-purpose physical range. */
    if (physical < 0x200000ULL) return (void*)(uintptr_t)physical;
    if (physmap_ready) {
        u32 pdpt_index = (u32)(physical >> 30);
        u32 pd_index = (u32)((physical >> 21) & 0x1FF);
        if (!(physmap_pd[pdpt_index][pd_index] & PTE_PRESENT)) return NULL;
        return (void*)(uintptr_t)(physmap_base + physical);
    }
    if (physical < 0x100000000ULL) return (void*)(uintptr_t)physical;
    return NULL;
}

static bool vmm_is_canonical(u64 address) {
    u64 upper = address >> 48;
    bool sign = (address & (1ULL << 47)) != 0;
    return sign ? upper == 0xFFFFULL : upper == 0;
}

/* A 4 KiB mapping must fit wholly inside one canonical x86-64 address range. */
static bool vmm_valid_page_address(u64 address) {
    if (address & (PAGE_SIZE - 1)) return false;
    if (address > UINT64_MAX - (PAGE_SIZE - 1)) return false;
    return vmm_is_canonical(address) &&
           vmm_is_canonical(address + PAGE_SIZE - 1);
}

static bool vmm_valid_mapping(u64 virt, u64 phys, u64 flags) {
    if (!vmm_valid_page_address(virt) ||
        (phys & (PAGE_SIZE - 1)) ||
        phys > 0x000FFFFFFFFFF000ULL) return false;

    /* Ring 3 is confined to the lower canonical half. Never let a mistaken
     * USER flag relax PML4 permissions for the kernel, direct map, heap or
     * device windows. */
    if ((flags & PTE_USER) && virt >= 0x0000800000000000ULL) return false;
    return true;
}

static inline u64* phys_to_virt_pt(u64 phys) {
    return (u64*)vmm_phys_to_virt(phys & ~0xFFFULL);
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

void vmm_init(void) {
    /* Bootstrap page tables and kernel image are identity mapped below 4 GiB.
     * Install the high-half direct map before PMM frames are dereferenced. */
    u64 boot_cr3 = get_cr3() & ~0xFFFULL;
    if (boot_cr3 >= 0x100000000ULL ||
        (u64)(uintptr_t)physmap_pdpt >= 0x100000000ULL ||
        (u64)(uintptr_t)physmap_pd >= 0x100000000ULL) {
        vga_puts("VMM: bootstrap structures outside 4 GiB identity map\n");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }

    u64* pml4 = (u64*)(uintptr_t)boot_cr3;
    s32 slot = -1;
    for (s32 candidate = 510; candidate >= 256; candidate--) {
        if (!(pml4[candidate] & PTE_PRESENT)) {
            slot = candidate;
            break;
        }
    }
    if (slot < 0) {
        vga_puts("VMM: no free high-half PML4 slot for physical map\n");
        for (;;) asm volatile("cli; hlt" : : : "memory");
    }

    kmemset(physmap_pdpt, 0, sizeof(physmap_pdpt));
    kmemset(physmap_pd, 0, sizeof(physmap_pd));
    for (u32 i = 0; i < PHYSMAP_PDPT_ENTRIES; i++) {
        physmap_pdpt[i] = (u64)(uintptr_t)&physmap_pd[i] |
                          PTE_PRESENT | PTE_WRITABLE;
        for (u32 j = 0; j < 512; j++) {
            u64 physical = ((u64)i << 30) | ((u64)j << 21);
            /* The first 2 MiB contains bootstrap page tables and low kernel data.
             * It is excluded from PMM, but must remain addressable by the VMM. */
            physmap_pd[i][j] = pmm_block_is_usable_ram(physical)
                ? (physical | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE | PTE_NX)
                : 0;
        }
    }

    physmap_base = 0xFFFF000000000000ULL | ((u64)slot << 39);
    pml4[slot] = (u64)(uintptr_t)physmap_pdpt | PTE_PRESENT | PTE_WRITABLE;
    kernel_cr3 = boot_cr3;
    physmap_ready = true;
    flush_tlb();

    vga_printf("VMM: %u GiB RAM direct map at %p\n",
               (u32)(VMM_PHYSICAL_LIMIT >> 30),
               (void*)(uintptr_t)physmap_base);
    vga_puts("VMM initialized\n");
}

// Get or create a lower-level page table
static bool get_or_create_table(u64* parent_table, u16 idx, u64 flags) {
    if (!parent_table) return false;
    if (parent_table[idx] & PTE_PRESENT) return true;

    u64 phys = pmm_alloc_page();
    if (!phys) return false;
    u64* table = phys_to_virt_pt(phys);
    if (!table) {
        pmm_free_page(phys);
        return false;
    }
    kmemset(table, 0, PAGE_SIZE);
    parent_table[idx] = (phys & ~0xFFFULL) | PTE_PRESENT |
                        (flags & PTE_USER) | PTE_WRITABLE;
    return true;
}


bool vmm_map_page(u64 virt, u64 phys, u64 flags) {
    if (!vmm_valid_mapping(virt, phys, flags)) return false;
    u64* pml4 = phys_to_virt_pt(get_cr3());
    if (!pml4) return false;

    u16 i1 = PML4_INDEX(virt);
    if (!get_or_create_table(pml4, i1, 0)) return false;
    u64 pml4e = pml4[i1];
    if (pml4e & PTE_HUGE) return false;
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return false;

    u16 i2 = PDPT_INDEX(virt);
    if (!get_or_create_table(pdpt, i2, 0)) return false;
    u64 pdpte = pdpt[i2];
    if (pdpte & PTE_HUGE) return false;
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    if (!pd) return false;

    u16 i3 = PD_INDEX(virt);
    if (!get_or_create_table(pd, i3, 0)) return false;
    u64 pde = pd[i3];
    if (pde & PTE_HUGE) return false;
    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    if (!pt) return false;

    if (flags & PTE_USER) {
        pml4[i1] |= PTE_USER;
        pdpt[i2] |= PTE_USER;
        pd[i3] |= PTE_USER;
    }
    pt[PT_INDEX(virt)] = (phys & ~0xFFFULL) | PTE_PRESENT |
        (flags & (PTE_WRITABLE | PTE_USER | PTE_NX | (1ULL << 3) | (1ULL << 4)));
    flush_tlb();
    return true;
}

void vmm_unmap_page(u64 virt) {
    u64* pml4 = phys_to_virt_pt(get_cr3());
    if (!pml4) return;
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT) || (pml4e & PTE_HUGE)) return;

    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return;
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT) || (pdpte & PTE_HUGE)) return;

    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    if (!pd) return;
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) return;

    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    if (!pt) return;
    pt[PT_INDEX(virt)] = 0;
    flush_tlb();
}

/* Remove a 4 KiB leaf mapping from a specified address space. This does not
 * free the mapped physical page or intermediate page-table pages; ownership
 * stays with the caller. Returns the mapped physical page base, or 0 if the
 * address was not mapped as a 4 KiB page. */
u64 vmm_unmap_page_pml4(pml4_t* pml4, u64 virt) {
    if (!pml4) return 0;
    u64* pml4_table = phys_to_virt_pt((u64)pml4);
    if (!pml4_table) return 0;

    u64 pml4e = pml4_table[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT) || (pml4e & PTE_HUGE)) return 0;
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return 0;

    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT) || (pdpte & PTE_HUGE)) return 0;
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    if (!pd) return 0;

    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) return 0;
    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    if (!pt) return 0;

    u64* pte = &pt[PT_INDEX(virt)];
    if (!(*pte & PTE_PRESENT)) return 0;
    u64 phys = *pte & 0x000FFFFFFFFFF000ULL;
    *pte = 0;
    if (get_cr3() == ((u64)pml4 & ~0xFFFULL)) {
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
    return phys;
}

u64 vmm_get_phys(u64 virt) {
    u64* pml4 = phys_to_virt_pt(get_cr3());
    if (!pml4) return 0;
    u64 pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & PTE_PRESENT)) return 0;

    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return 0;
    u64 pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & PTE_PRESENT)) return 0;
    if (pdpte & PTE_HUGE) {
        return (pdpte & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFFULL);
    }

    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    if (!pd) return 0;
    u64 pde = pd[PD_INDEX(virt)];
    if (!(pde & PTE_PRESENT)) return 0;
    if (pde & PTE_HUGE) {
        return (pde & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
    }

    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    if (!pt) return 0;
    u64 pte = pt[PT_INDEX(virt)];
    if (!(pte & PTE_PRESENT)) return 0;
    return (pte & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFFULL);
}

// Walk the page table tree and return the PTE for a virtual address.
// If create=true, allocate missing intermediate tables.
pte_t* vmm_walk(pml4_t* pml4, u64 vaddr, bool create) {
    if (!pml4) return NULL;
    u64* pml4_table = phys_to_virt_pt((u64)pml4);
    if (!pml4_table) return NULL;

    u16 i1 = PML4_INDEX(vaddr);
    if (!(pml4_table[i1] & PTE_PRESENT)) {
        if (!create || !get_or_create_table(pml4_table, i1, 0)) return NULL;
    }
    u64 pml4e = pml4_table[i1];
    if (pml4e & PTE_HUGE) return NULL;
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return NULL;

    u16 i2 = PDPT_INDEX(vaddr);
    if (!(pdpt[i2] & PTE_PRESENT)) {
        if (!create || !get_or_create_table(pdpt, i2, 0)) return NULL;
    }
    u64 pdpte = pdpt[i2];
    if (pdpte & PTE_HUGE) return NULL;
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    if (!pd) return NULL;

    u16 i3 = PD_INDEX(vaddr);
    if (!(pd[i3] & PTE_PRESENT)) {
        if (!create || !get_or_create_table(pd, i3, 0)) return NULL;
    }
    u64 pde = pd[i3];
    if (pde & PTE_HUGE) return NULL;
    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    if (!pt) return NULL;
    return &pt[PT_INDEX(vaddr)];
}

/*
 * Translate a virtual address without assuming every mapping is 4 KiB.
 * Large PDPTE/PDE entries are valid leaves. Permission bits are effective
 * only when every level in the walk grants them: USER/WRITABLE are ANDed,
 * while NX is restrictive and therefore ORed.
 */
bool vmm_translate(pml4_t* pml4, u64 vaddr, u64* physical, u64* flags) {
    if (!pml4 || !physical || !flags) return false;
    const u64* pml4_table = (const u64*)vmm_phys_to_virt((u64)pml4);
    if (!pml4_table) return false;

    u64 pml4e = pml4_table[PML4_INDEX(vaddr)];
    if (!(pml4e & PTE_PRESENT)) return false;
    const u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return false;

    u64 pdpte = pdpt[PDPT_INDEX(vaddr)];
    if (!(pdpte & PTE_PRESENT)) return false;

    bool user = (pml4e & PTE_USER) && (pdpte & PTE_USER);
    bool writable = (pml4e & PTE_WRITABLE) && (pdpte & PTE_WRITABLE);
    bool nx = ((pml4e | pdpte) & PTE_NX) != 0;
    u64 translated;

    if (pdpte & PTE_HUGE) {
        translated = (pdpte & 0x000FFFFFC0000000ULL) |
                     (vaddr & 0x3FFFFFFFULL);
    } else {
        const u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
        if (!pd) return false;
        u64 pde = pd[PD_INDEX(vaddr)];
        if (!(pde & PTE_PRESENT)) return false;

        user = user && ((pde & PTE_USER) != 0);
        writable = writable && ((pde & PTE_WRITABLE) != 0);
        nx = nx || ((pde & PTE_NX) != 0);

        if (pde & PTE_HUGE) {
            translated = (pde & 0x000FFFFFFFE00000ULL) |
                         (vaddr & 0x1FFFFFULL);
        } else {
            const u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
            if (!pt) return false;
            u64 pte = pt[PT_INDEX(vaddr)];
            if (!(pte & PTE_PRESENT)) return false;

            user = user && ((pte & PTE_USER) != 0);
            writable = writable && ((pte & PTE_WRITABLE) != 0);
            nx = nx || ((pte & PTE_NX) != 0);
            translated = (pte & 0x000FFFFFFFFFF000ULL) |
                         (vaddr & 0xFFFULL);
        }
    }

    *physical = translated;
    *flags = VMM_FLAG_PRESENT |
             (user ? VMM_FLAG_USER : 0) |
             (writable ? VMM_FLAG_WRITABLE : 0) |
             (nx ? VMM_FLAG_NX : 0);
    return true;
}

// Create a new empty page table (for new processes).
/* Caller must switch away from this address space before destroying it. */
void vmm_destroy_address_space(pml4_t* pml4) {
    if (!pml4) return;
    u64 root_phys = (u64)(uintptr_t)pml4 & ~0xFFFULL;
    if (root_phys == kernel_cr3) return;

    u64* root = phys_to_virt_pt(root_phys);
    if (!root) return;

    /* Lower-half tables are private to the process. High-half entries are
     * shared with the kernel and intentionally left untouched. */
    for (u32 i = 0; i < 256; i++) {
        u64 pml4e = root[i];
        if (!(pml4e & PTE_PRESENT) || (pml4e & PTE_HUGE)) continue;
        u64 pdpt_phys = pml4e & 0x000FFFFFFFFFF000ULL;
        u64* pdpt = phys_to_virt_pt(pdpt_phys);
        if (!pdpt) {
            pmm_free_page(pdpt_phys);
            root[i] = 0;
            continue;
        }

        for (u32 j = 0; j < 512; j++) {
            u64 pdpte = pdpt[j];
            if (!(pdpte & PTE_PRESENT) || (pdpte & PTE_HUGE)) continue;
            u64 pd_phys = pdpte & 0x000FFFFFFFFFF000ULL;
            u64* pd = phys_to_virt_pt(pd_phys);
            if (!pd) {
                pmm_free_page(pd_phys);
                pdpt[j] = 0;
                continue;
            }

            for (u32 k = 0; k < 512; k++) {
                u64 pde = pd[k];
                if (!(pde & PTE_PRESENT) || (pde & PTE_HUGE)) continue;
                u64 pt_phys = pde & 0x000FFFFFFFFFF000ULL;
                u64* pt = phys_to_virt_pt(pt_phys);
                if (!pt) {
                    pmm_free_page(pt_phys);
                    pd[k] = 0;
                    continue;
                }

                /* Only USER leaf frames belong to the process. Supervisor
                 * identity mappings are borrowed from the kernel and remain. */
                for (u32 l = 0; l < 512; l++) {
                    u64 pte = pt[l];
                    if ((pte & (PTE_PRESENT | PTE_USER)) ==
                        (PTE_PRESENT | PTE_USER)) {
                        pmm_free_page(pte & 0x000FFFFFFFFFF000ULL);
                    }
                }
                pmm_free_page(pt_phys);
                pd[k] = 0;
            }
            pmm_free_page(pd_phys);
            pdpt[j] = 0;
        }
        pmm_free_page(pdpt_phys);
        root[i] = 0;
    }

    pmm_free_page(root_phys);
}

pml4_t* vmm_create_address_space(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return NULL;
    u64* pml4 = phys_to_virt_pt(phys);
    if (!pml4) {
        pmm_free_page(phys);
        return NULL;
    }
    kmemset(pml4, 0, PAGE_SIZE);
    return (pml4_t*)phys;
}

// Copy the low identity map while giving each process private lower-level
// tables, so changing user permissions cannot alter the kernel's page tables.
bool vmm_copy_kernel_mappings(pml4_t* dst, pml4_t* src) {
    if (!dst) return false;

    u64* dst_pml4 = phys_to_virt_pt((u64)dst);
    u64* src_pml4 = phys_to_virt_pt(src ? (u64)src : get_cr3());
    if (!dst_pml4 || !src_pml4) return false;

    /* Share only supervisor-only high-half mappings, including the direct
     * map. A USER bit here would expose the kernel to every ring-3 process. */
    for (u32 i = 256; i < 512; i++) {
        if (src_pml4[i] & PTE_USER) return false;
        dst_pml4[i] = src_pml4[i];
    }

    /* Clone the low identity-map hierarchy. Build only private child tables:
     * never leave pointers to source tables in a partially built destination.
     * This makes vmm_destroy_address_space() safe on every failure path. */
    u64 pml4e = src_pml4[0];
    if (!(pml4e & PTE_PRESENT)) return true;
    if (pml4e & PTE_USER) return false;

    u64 pdpt_phys = pmm_alloc_page();
    if (!pdpt_phys) return false;
    u64* src_pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    u64* dst_pdpt = phys_to_virt_pt(pdpt_phys);
    if (!src_pdpt || !dst_pdpt) {
        pmm_free_page(pdpt_phys);
        return false;
    }
    kmemset(dst_pdpt, 0, PAGE_SIZE);
    dst_pml4[0] = pdpt_phys | (pml4e & 0x8000000000000FFFULL);

    for (u32 i = 0; i < 512; i++) {
        u64 pdpte = src_pdpt[i];
        if (!(pdpte & PTE_PRESENT)) continue;
        if (pdpte & PTE_USER) return false;
        if (pdpte & PTE_HUGE) {
            dst_pdpt[i] = pdpte;
            continue;
        }

        u64 pd_phys = pmm_alloc_page();
        if (!pd_phys) return false;
        u64* src_pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
        u64* dst_pd = phys_to_virt_pt(pd_phys);
        if (!src_pd || !dst_pd) {
            pmm_free_page(pd_phys);
            return false;
        }
        kmemset(dst_pd, 0, PAGE_SIZE);
        dst_pdpt[i] = pd_phys | (pdpte & 0x8000000000000FFFULL);

        for (u32 j = 0; j < 512; j++) {
            u64 pde = src_pd[j];
            if (!(pde & PTE_PRESENT)) continue;
            if (pde & PTE_USER) return false;
            if (pde & PTE_HUGE) {
                dst_pd[j] = pde;
                continue;
            }

            u64 pt_phys = pmm_alloc_page();
            if (!pt_phys) return false;
            u64* src_pt = phys_to_virt_pt(pde & ~0xFFFULL);
            u64* dst_pt = phys_to_virt_pt(pt_phys);
            if (!src_pt || !dst_pt) {
                pmm_free_page(pt_phys);
                return false;
            }
            kmemset(dst_pt, 0, PAGE_SIZE);
            dst_pd[j] = pt_phys | (pde & 0x8000000000000FFFULL);
            for (u32 k = 0; k < 512; k++) {
                if (src_pt[k] & PTE_USER) return false;
                dst_pt[k] = src_pt[k];
            }
        }
    }
    return true;
}

// Switch the CPU to use a given page table.
void vmm_switch(pml4_t* pml4) {
    if (!kernel_cr3) kernel_cr3 = get_cr3();
    u64 cr3 = pml4 ? (u64)pml4 : kernel_cr3;
    asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

// Map a page into a specific address space (not current)
bool vmm_map_page_pml4(pml4_t* pml4, u64 virt, u64 phys, u64 flags) {
    if (!pml4 || !vmm_valid_mapping(virt, phys, flags)) return false;
    u64* pml4_table = phys_to_virt_pt((u64)pml4);
    if (!pml4_table) return false;

    u16 i1 = PML4_INDEX(virt);
    if (!get_or_create_table(pml4_table, i1, 0)) return false;
    u64 pml4e = pml4_table[i1];
    if (pml4e & PTE_HUGE) return false;
    u64* pdpt = phys_to_virt_pt(pml4e & ~0xFFFULL);
    if (!pdpt) return false;

    u16 i2 = PDPT_INDEX(virt);
    if (!get_or_create_table(pdpt, i2, 0)) return false;
    u64 pdpte = pdpt[i2];
    if (pdpte & PTE_HUGE) return false; /* 1 GiB splitting is not implemented. */
    u64* pd = phys_to_virt_pt(pdpte & ~0xFFFULL);
    if (!pd) return false;

    u16 i3 = PD_INDEX(virt);
    if (!get_or_create_table(pd, i3, 0)) return false;
    u64 pde = pd[i3];
    if (pde & PTE_HUGE) {
        u64 pt_phys = pmm_alloc_page();
        if (!pt_phys) return false;
        u64* pt = phys_to_virt_pt(pt_phys);
        if (!pt) {
            pmm_free_page(pt_phys);
            return false;
        }
        u64 base = pde & 0x000FFFFFFFE00000ULL;
        u64 page_flags = pde & 0x8000000000000FFFULL;
        page_flags &= ~PTE_HUGE;
        for (u32 i = 0; i < 512; i++) {
            pt[i] = (base + (u64)i * PAGE_SIZE) | page_flags;
        }
        pde = pt_phys | (pde & 0x8000000000000FFFULL & ~PTE_HUGE);
        pd[i3] = pde;
    }

    u64* pt = phys_to_virt_pt(pde & ~0xFFFULL);
    if (!pt) return false;

    if (flags & VMM_FLAG_USER) {
        pml4_table[i1] |= PTE_USER;
        pdpt[i2] |= PTE_USER;
        pd[i3] |= PTE_USER;
    }
    pt[PT_INDEX(virt)] = (phys & ~0xFFFULL) | PTE_PRESENT |
        (flags & (PTE_WRITABLE | PTE_USER | PTE_NX | PTE_WRITETHROUGH | PTE_NOCACHE));

    if (get_cr3() == ((u64)pml4 & ~0xFFFULL)) {
        asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
    return true;
}
