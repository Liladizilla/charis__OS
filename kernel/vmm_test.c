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

    u64 virt = 0xFFFFA00000000000ULL;

    if (!vmm_map_page(virt, phys, PTE_WRITABLE)) return false;
    u64 phys2 = vmm_get_phys(virt);
    if (phys2 != (phys & ~0xFFFULL)) return false;

    // map same page again
    return vmm_map_page(virt, phys, PTE_WRITABLE);
}

static bool test_map_missing_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    u64 virt = 0xFFFFA00000001000ULL;

    // Should create missing tables and then map
    if (!vmm_map_page(virt, phys, PTE_WRITABLE)) return false;

    u64 phys2 = vmm_get_phys(virt);
    return phys2 == (phys & ~0xFFFULL);
}

static bool test_remap_same_page(void) {
    u64 phys1 = pmm_alloc_page();
    u64 phys2 = pmm_alloc_page();
    if (!phys1 || !phys2) return false;

    u64 virt = 0xFFFFA00000002000ULL;

    if (!vmm_map_page(virt, phys1, PTE_WRITABLE)) return false;
    if (!vmm_map_page(virt, phys2, PTE_WRITABLE)) return false;

    u64 phys2_ret = vmm_get_phys(virt);
    return phys2_ret == (phys2 & ~0xFFFULL);
}

static bool test_unmap_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;

    u64 virt = 0xFFFFA00000003000ULL;

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
    u64* pml4 = (u64*)cr3;
    u64 pml4e = pml4[(virt >> 39) & 0x1FF];
    if (!(pml4e & (1ULL << 2))) return false;

    u64* pdpt = (u64*)(pml4e & ~0xFFFULL);
    u64 pdpte = pdpt[(virt >> 30) & 0x1FF];
    if (!(pdpte & (1ULL << 2))) return false;

    u64* pd = (u64*)(pdpte & ~0xFFFULL);
    u64 pde = pd[(virt >> 21) & 0x1FF];
    if (!(pde & (1ULL << 2))) return false;

    u64* pt = (u64*)(pde & ~0xFFFULL);
    u64 pte = pt[(virt >> 12) & 0x1FF];
    if (!(pte & (1ULL << 2))) return false;

    u64 translated = 0, effective = 0;
    if (!vmm_translate((pml4_t*)pml4, virt, &translated, &effective)) return false;
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

    u64* pml4 = (u64*)pml4_phys;
    u64* pdpt_2m = (u64*)pdpt_2m_phys;
    u64* pd_2m = (u64*)pd_2m_phys;
    u64* pdpt_1g = (u64*)pdpt_1g_phys;
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

    bool rejects_2m = vmm_walk((pml4_t*)pml4, 0x1000, false) == NULL;
    bool rejects_1g = vmm_walk((pml4_t*)pml4,
                                0x0000008000001234ULL, false) == NULL;

    u64 phys_2m = 0, flags_2m = 0;
    u64 phys_1g = 0, flags_1g = 0;
    bool translates_2m =
        vmm_translate((pml4_t*)pml4, 0x1000, &phys_2m, &flags_2m) &&
        phys_2m == 0x00201000ULL && !(flags_2m & VMM_FLAG_USER);
    bool translates_1g =
        vmm_translate((pml4_t*)pml4, 0x0000008000001234ULL,
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

    u64* pml4 = (u64*)pml4_phys;
    u64* pdpt = (u64*)pdpt_phys;
    u64* pd = (u64*)pd_phys;
    u64* pt = (u64*)pt_phys;
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
    bool translated = vmm_translate((pml4_t*)pml4, 0x1000, &physical, &flags);
    bool denied_user = translated && physical == 0x00400000ULL &&
                       !(flags & VMM_FLAG_USER);

    pmm_free_page(pt_phys);
    pmm_free_page(pd_phys);
    pmm_free_page(pdpt_phys);
    pmm_free_page(pml4_phys);
    return denied_user;
}

void vmm_run_tests(void) {
    report("map_existing", test_map_existing());
    report("map_missing", test_map_missing_page());
    report("remap_same", test_remap_same_page());
    report("unmap_page", test_unmap_page());
    report("map_user_page", test_map_user_page());
    report("walk_rejects_huge", test_walk_rejects_huge_leaves());
    report("translate_effective_user", test_translate_requires_user_at_every_level());
}

