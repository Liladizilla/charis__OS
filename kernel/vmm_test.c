#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/memory.h>
#include <kernel/serial.h>
#include <kernel/vga.h>
#include <kernel/types.h>
#include <kernel/string.h>

static void report(const char* name, bool ok) {
    const char* result = ok ? "OK" : "FAIL";
    vga_puts("[VMM] ");
    vga_puts(name);
    vga_puts(": ");
    vga_puts(result);
    vga_puts("\n");
    /* The CI boot gate reads the COM1 log, not a framebuffer screenshot. */
    serial_printf("[VMM] %s: %s\n", name, result);
}

static bool test_map_existing(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    u64 virt = 0xFFFFB00000000000ULL;

    if (!vmm_map_page(virt, phys, PTE_WRITABLE)) return false;
    u64 phys2 = vmm_get_phys(virt);
    if (phys2 != (phys & ~0xFFFULL)) return false;

    // map same page again
    return vmm_map_page(virt, phys, PTE_WRITABLE);
}

static bool test_map_missing_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    u64 virt = 0xFFFFB00000001000ULL;

    // Should create missing tables and then map
    if (!vmm_map_page(virt, phys, PTE_WRITABLE)) return false;

    u64 phys2 = vmm_get_phys(virt);
    return phys2 == (phys & ~0xFFFULL);
}

static bool test_remap_same_page(void) {
    u64 phys1 = pmm_alloc_page();
    u64 phys2 = pmm_alloc_page();
    if (!phys1 || !phys2) return false;

    u64 virt = 0xFFFFB00000002000ULL;

    if (!vmm_map_page(virt, phys1, PTE_WRITABLE)) return false;
    if (!vmm_map_page(virt, phys2, PTE_WRITABLE)) return false;

    u64 phys2_ret = vmm_get_phys(virt);
    return phys2_ret == (phys2 & ~0xFFFULL);
}

static bool test_unmap_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    u64 virt = 0xFFFFB00000003000ULL;

    if (!vmm_map_page(virt, phys, PTE_WRITABLE)) return false;

    vmm_unmap_page(virt);
    return vmm_get_phys(virt) == 0;
}

static bool test_map_user_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    /* Use an unused lower-half PML4 slot. Testing a USER mapping in the
     * kernel's shared high-half slot would unnecessarily relax upper-level
     * permissions for the kernel mapping tree. */
    u64 virt = 0x0000400000004000ULL;

    if (!vmm_map_page(virt, phys, PTE_WRITABLE | PTE_USER)) return false;
    if (vmm_get_phys(virt) != (phys & ~0xFFFULL)) return false;

    u64 cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    u64* pml4 = (u64*)vmm_phys_to_virt(cr3 & ~0xFFFULL);
    if (!pml4) return false;
    u64 pml4e = pml4[(virt >> 39) & 0x1FF];
    if (!(pml4e & (1ULL << 2))) return false;

    u64* pdpt = (u64*)vmm_phys_to_virt(pml4e & ~0xFFFULL);
    if (!pdpt) return false;
    u64 pdpte = pdpt[(virt >> 30) & 0x1FF];
    if (!(pdpte & (1ULL << 2))) return false;

    u64* pd = (u64*)vmm_phys_to_virt(pdpte & ~0xFFFULL);
    if (!pd) return false;
    u64 pde = pd[(virt >> 21) & 0x1FF];
    if (!(pde & (1ULL << 2))) return false;

    u64* pt = (u64*)vmm_phys_to_virt(pde & ~0xFFFULL);
    if (!pt) return false;
    u64 pte = pt[(virt >> 12) & 0x1FF];
    if (!(pte & (1ULL << 2))) return false;

    u64 translated = 0, effective = 0;
    if (!vmm_translate((pml4_t*)(cr3 & ~0xFFFULL), virt, &translated, &effective)) return false;
    return translated == (phys & ~0xFFFULL) &&
           (effective & (VMM_FLAG_USER | VMM_FLAG_WRITABLE)) ==
           (VMM_FLAG_USER | VMM_FLAG_WRITABLE);
}

static bool test_walk_rejects_huge_leaves(void) {
    u64 pml4_phys = pmm_alloc_page();
    u64 pdpt_2m_phys = pmm_alloc_page();
    u64 pd_2m_phys = pmm_alloc_page();
    u64 pdpt_1g_phys = pmm_alloc_page();
    if (!pml4_phys || !pdpt_2m_phys || !pd_2m_phys || !pdpt_1g_phys) {
        if (pml4_phys) pmm_free_page(pml4_phys);
        if (pdpt_2m_phys) pmm_free_page(pdpt_2m_phys);
        if (pd_2m_phys) pmm_free_page(pd_2m_phys);
        if (pdpt_1g_phys) pmm_free_page(pdpt_1g_phys);
        return false;
    }

    u64* pml4 = (u64*)vmm_phys_to_virt(pml4_phys);
    u64* pdpt_2m = (u64*)vmm_phys_to_virt(pdpt_2m_phys);
    u64* pd_2m = (u64*)vmm_phys_to_virt(pd_2m_phys);
    u64* pdpt_1g = (u64*)vmm_phys_to_virt(pdpt_1g_phys);
    if (!pml4 || !pdpt_2m || !pd_2m || !pdpt_1g) {
        pmm_free_page(pdpt_1g_phys);
        pmm_free_page(pd_2m_phys);
        pmm_free_page(pdpt_2m_phys);
        pmm_free_page(pml4_phys);
        return false;
    }
    kmemset(pml4, 0, PAGE_SIZE);
    kmemset(pdpt_2m, 0, PAGE_SIZE);
    kmemset(pd_2m, 0, PAGE_SIZE);
    kmemset(pdpt_1g, 0, PAGE_SIZE);

    /* Lower-half 2 MiB huge PDE at virtual address 0x1000. */
    pml4[0] = pdpt_2m_phys | PTE_PRESENT | PTE_WRITABLE;
    pdpt_2m[0] = pd_2m_phys | PTE_PRESENT | PTE_WRITABLE;
    pd_2m[0] = 0x00200000ULL | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;

    /* A separate PML4 slot containing a 1 GiB huge PDPTE. */
    pml4[1] = pdpt_1g_phys | PTE_PRESENT | PTE_WRITABLE;
    pdpt_1g[0] = 0x40000000ULL | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;

    bool rejects_2m = vmm_walk((pml4_t*)pml4_phys, 0x1000, false) == NULL;
    bool rejects_1g = vmm_walk((pml4_t*)pml4_phys,
                                0x0000008000001234ULL, false) == NULL;

    u64 phys_2m = 0, flags_2m = 0;
    u64 phys_1g = 0, flags_1g = 0;
    bool translates_2m =
        vmm_translate((pml4_t*)pml4_phys, 0x1000, &phys_2m, &flags_2m) &&
        phys_2m == 0x00201000ULL && !(flags_2m & VMM_FLAG_USER);
    bool translates_1g =
        vmm_translate((pml4_t*)pml4_phys, 0x0000008000001234ULL,
                      &phys_1g, &flags_1g) &&
        phys_1g == 0x40001234ULL && !(flags_1g & VMM_FLAG_USER);

    pmm_free_page(pdpt_1g_phys);
    pmm_free_page(pd_2m_phys);
    pmm_free_page(pdpt_2m_phys);
    pmm_free_page(pml4_phys);
    return rejects_2m && rejects_1g && translates_2m && translates_1g;
}

static bool test_translate_requires_user_at_every_level(void) {
    u64 pml4_phys = pmm_alloc_page();
    u64 pdpt_phys = pmm_alloc_page();
    u64 pd_phys = pmm_alloc_page();
    u64 pt_phys = pmm_alloc_page();
    if (!pml4_phys || !pdpt_phys || !pd_phys || !pt_phys) {
        if (pml4_phys) pmm_free_page(pml4_phys);
        if (pdpt_phys) pmm_free_page(pdpt_phys);
        if (pd_phys) pmm_free_page(pd_phys);
        if (pt_phys) pmm_free_page(pt_phys);
        return false;
    }

    u64* pml4 = (u64*)vmm_phys_to_virt(pml4_phys);
    u64* pdpt = (u64*)vmm_phys_to_virt(pdpt_phys);
    u64* pd = (u64*)vmm_phys_to_virt(pd_phys);
    u64* pt = (u64*)vmm_phys_to_virt(pt_phys);
    if (!pml4 || !pdpt || !pd || !pt) {
        pmm_free_page(pt_phys);
        pmm_free_page(pd_phys);
        pmm_free_page(pdpt_phys);
        pmm_free_page(pml4_phys);
        return false;
    }
    kmemset(pml4, 0, PAGE_SIZE);
    kmemset(pdpt, 0, PAGE_SIZE);
    kmemset(pd, 0, PAGE_SIZE);
    kmemset(pt, 0, PAGE_SIZE);

    /* The leaf and lower tables say USER, but the PML4 entry is supervisor. */
    pml4[0] = pdpt_phys | PTE_PRESENT | PTE_WRITABLE;
    pdpt[0] = pd_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pd[0] = pt_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pt[1] = 0x00400000ULL | PTE_PRESENT | PTE_WRITABLE | PTE_USER;

    u64 physical = 0, flags = 0;
    bool translated = vmm_translate((pml4_t*)pml4_phys, 0x1000, &physical, &flags);
    bool denied_user = translated && physical == 0x00400000ULL &&
                       !(flags & VMM_FLAG_USER);

    pmm_free_page(pt_phys);
    pmm_free_page(pd_phys);
    pmm_free_page(pdpt_phys);
    pmm_free_page(pml4_phys);
    return denied_user;
}


static bool test_supervisor_direct_map_above_4g(void) {
    /* Prefer a real RAM block above 4 GiB when the machine has one. Smaller
     * CI/VM profiles fall back to an allocated page, while still exercising
     * the exact same direct-map translation and supervisor permissions. */
    u64 physical = 0;
    for (u64 block = 0x100000000ULL;
         block <= VMM_PHYSICAL_LIMIT - (2ULL * 1024ULL * 1024ULL);
         block += 2ULL * 1024ULL * 1024ULL) {
        if (pmm_block_is_usable_ram(block)) {
            physical = block + 0x1234ULL;
            break;
        }
    }

    bool allocated_fallback = false;
    if (!physical) {
        physical = pmm_alloc_page();
        if (!physical) return false;
        allocated_fallback = true;
    }

    void* alias = vmm_phys_to_virt(physical);
    bool ok = alias != NULL;
    u64 cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    u64 cr3_phys = cr3 & ~0xFFFULL;
    void* cr3_alias = vmm_phys_to_virt(cr3_phys);
    if (!cr3_alias || vmm_get_phys((u64)(uintptr_t)cr3_alias) != cr3_phys) ok = false;
    u64 translated = 0, flags = 0;
    if (ok) {
        ok = vmm_get_phys((u64)(uintptr_t)alias) == physical &&
             vmm_translate((pml4_t*)(cr3 & ~0xFFFULL),
                           (u64)(uintptr_t)alias, &translated, &flags) &&
             translated == physical &&
             !(flags & VMM_FLAG_USER) &&
             (flags & VMM_FLAG_WRITABLE) &&
             (flags & VMM_FLAG_NX) &&
             vmm_phys_to_virt(VMM_PHYSICAL_LIMIT) == NULL;
    }

    if (allocated_fallback) pmm_free_page(physical & ~0xFFFULL);
    return ok;
}

static bool test_pmm_returns_direct_mapped_ram(void) {
    u64 physical = pmm_alloc_page();
    if (!physical) return false;
    bool ok = pmm_block_is_usable_ram(physical & ~((2ULL * 1024ULL * 1024ULL) - 1)) &&
              vmm_phys_to_virt(physical) != NULL &&
              physical < VMM_PHYSICAL_LIMIT;
    pmm_free_page(physical);
    return ok;
}


static bool test_pmm_cannot_free_reserved_frame(void) {
    u32 used_before = pmm_used_pages();
    pmm_free_page(0x00100000ULL); /* kernel image reservation */
    u32 used_after = pmm_used_pages();
    return used_before == used_after;
}

void vmm_run_tests(void) {
    report("map_existing", test_map_existing());
    report("map_missing", test_map_missing_page());
    report("remap_same", test_remap_same_page());
    report("unmap_page", test_unmap_page());
    report("map_user_page", test_map_user_page());
    report("walk_rejects_huge", test_walk_rejects_huge_leaves());
    report("translate_effective_user", test_translate_requires_user_at_every_level());
    report("physmap_supervisor_alias", test_supervisor_direct_map_above_4g());
    report("pmm_direct_mapped_ram", test_pmm_returns_direct_mapped_ram());
    report("pmm_reserved_frame_protected", test_pmm_cannot_free_reserved_frame());
}

