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
    bool ok = false;

    if (vmm_map_page(virt, phys, PTE_WRITABLE)) {
        u64 phys2 = vmm_get_phys(virt);
        ok = phys2 == (phys & ~0xFFFULL) &&
             vmm_map_page(virt, phys, PTE_WRITABLE);
        vmm_unmap_page(virt);
    }
    pmm_free_page(phys);
    return ok;
}

static bool test_map_missing_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;
    u64 virt = 0xFFFFB00000001000ULL;
    bool ok = false;

    if (vmm_map_page(virt, phys, PTE_WRITABLE)) {
        ok = vmm_get_phys(virt) == (phys & ~0xFFFULL);
        vmm_unmap_page(virt);
    }
    pmm_free_page(phys);
    return ok;
}

static bool test_remap_same_page(void) {
    u64 phys1 = pmm_alloc_page();
    u64 phys2 = pmm_alloc_page();
    if (!phys1 || !phys2) {
        if (phys1) pmm_free_page(phys1);
        if (phys2) pmm_free_page(phys2);
        return false;
    }

    u64 virt = 0xFFFFB00000002000ULL;
    bool mapped = false, ok = false;
    if (vmm_map_page(virt, phys1, PTE_WRITABLE)) {
        mapped = true;
        if (vmm_map_page(virt, phys2, PTE_WRITABLE))
            ok = vmm_get_phys(virt) == (phys2 & ~0xFFFULL);
    }
    if (mapped) vmm_unmap_page(virt);
    pmm_free_page(phys1);
    pmm_free_page(phys2);
    return ok;
}

static bool test_unmap_page(void) {
    u64 phys = pmm_alloc_page();
    if (!phys) return false;
    u64 virt = 0xFFFFB00000003000ULL;
    bool ok = false;

    if (vmm_map_page(virt, phys, PTE_WRITABLE)) {
        vmm_unmap_page(virt);
        ok = vmm_get_phys(virt) == 0;
    }
    pmm_free_page(phys);
    return ok;
}

static bool test_map_user_page(void) {
    /* Exercise USER permissions in an isolated temporary PML4. Never leave a
     * USER leaf in the kernel CR3 where later process tables could inherit it. */
    u64 pml4_phys = pmm_alloc_page();
    u64 phys = pmm_alloc_page();
    if (!pml4_phys || !phys) {
        if (pml4_phys) pmm_free_page(pml4_phys);
        if (phys) pmm_free_page(phys);
        return false;
    }

    u64* pml4 = (u64*)vmm_phys_to_virt(pml4_phys);
    if (!pml4) {
        pmm_free_page(phys);
        pmm_free_page(pml4_phys);
        return false;
    }
    kmemset(pml4, 0, PAGE_SIZE);
    const u64 virt = 0x0000400000004000ULL;
    bool ok = vmm_map_page_pml4((pml4_t*)pml4_phys, virt, phys,
                                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);

    u64 pdpt_phys = 0, pd_phys = 0, pt_phys = 0;
    if (ok) {
        u64 pml4e = pml4[(virt >> 39) & 0x1FF];
        u64* pdpt = (u64*)vmm_phys_to_virt(pml4e & ~0xFFFULL);
        ok = (pml4e & PTE_USER) && pdpt;
        if (ok) {
            u64 pdpte = pdpt[(virt >> 30) & 0x1FF];
            pdpt_phys = pml4e & ~0xFFFULL;
            u64* pd = (u64*)vmm_phys_to_virt(pdpte & ~0xFFFULL);
            ok = (pdpte & PTE_USER) && pd;
            if (ok) {
                u64 pde = pd[(virt >> 21) & 0x1FF];
                pd_phys = pdpte & ~0xFFFULL;
                u64* pt = (u64*)vmm_phys_to_virt(pde & ~0xFFFULL);
                ok = (pde & PTE_USER) && pt;
                if (ok) {
                    pt_phys = pde & ~0xFFFULL;
                    u64 pte = pt[(virt >> 12) & 0x1FF];
                    u64 translated = 0, effective = 0;
                    ok = (pte & PTE_USER) &&
                         vmm_translate((pml4_t*)pml4_phys, virt,
                                       &translated, &effective) &&
                         translated == (phys & ~0xFFFULL) &&
                         (effective & (VMM_FLAG_USER | VMM_FLAG_WRITABLE)) ==
                         (VMM_FLAG_USER | VMM_FLAG_WRITABLE);
                }
            }
        }
    }

    u64 mapped = vmm_unmap_page_pml4((pml4_t*)pml4_phys, virt);
    pmm_free_page(mapped ? mapped : phys);
    if (pt_phys) pmm_free_page(pt_phys);
    if (pd_phys) pmm_free_page(pd_phys);
    if (pdpt_phys) pmm_free_page(pdpt_phys);
    pmm_free_page(pml4_phys);
    return ok;
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

static bool test_user_mapping_rejects_kernel_half(void) {
    u64 pml4_phys = pmm_alloc_page();
    u64 frame_phys = pmm_alloc_page();
    if (!pml4_phys || !frame_phys) {
        if (pml4_phys) pmm_free_page(pml4_phys);
        if (frame_phys) pmm_free_page(frame_phys);
        return false;
    }

    u64* pml4 = (u64*)vmm_phys_to_virt(pml4_phys);
    if (!pml4) {
        pmm_free_page(frame_phys);
        pmm_free_page(pml4_phys);
        return false;
    }
    kmemset(pml4, 0, PAGE_SIZE);

    const u64 kernel_va = 0xFFFF900000001000ULL;
    const u64 noncanonical_va = 0x0000800000001000ULL;
    u32 kernel_slot = (u32)((kernel_va >> 39) & 0x1FF);
    u32 invalid_slot = (u32)((noncanonical_va >> 39) & 0x1FF);

    bool kernel_rejected =
        !vmm_map_page_pml4((pml4_t*)pml4_phys, kernel_va, frame_phys,
                           VMM_FLAG_PRESENT | VMM_FLAG_USER) &&
        pml4[kernel_slot] == 0;
    bool noncanonical_rejected =
        !vmm_map_page_pml4((pml4_t*)pml4_phys, noncanonical_va, frame_phys,
                           VMM_FLAG_PRESENT | VMM_FLAG_USER) &&
        pml4[invalid_slot] == 0;

    pmm_free_page(frame_phys);
    pmm_free_page(pml4_phys);
    return kernel_rejected && noncanonical_rejected;
}

static bool test_vmm_rejects_unaligned_pages(void) {
    u64 pml4_phys = pmm_alloc_page();
    u64 frame_phys = pmm_alloc_page();
    if (!pml4_phys || !frame_phys) {
        if (pml4_phys) pmm_free_page(pml4_phys);
        if (frame_phys) pmm_free_page(frame_phys);
        return false;
    }

    u64* pml4 = (u64*)vmm_phys_to_virt(pml4_phys);
    if (!pml4) {
        pmm_free_page(frame_phys);
        pmm_free_page(pml4_phys);
        return false;
    }
    kmemset(pml4, 0, PAGE_SIZE);

    bool virtual_rejected =
        !vmm_map_page_pml4((pml4_t*)pml4_phys, 0x0000400000004001ULL,
                           frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER);
    bool physical_rejected =
        !vmm_map_page_pml4((pml4_t*)pml4_phys, 0x0000400000004000ULL,
                           frame_phys + 1, VMM_FLAG_PRESENT | VMM_FLAG_USER);
    u32 target_slot = (u32)((0x0000400000004000ULL >> 39) & 0x1FF);
    bool no_tables_created = pml4[target_slot] == 0;

    pmm_free_page(frame_phys);
    pmm_free_page(pml4_phys);
    return virtual_rejected && physical_rejected && no_tables_created;
}


/* Exercise the complete input-validation boundary without allowing a rejected
 * mapping to allocate or modify any page-table level. */
static bool test_mapping_validation_matrix(void) {
    u64 pml4_phys = pmm_alloc_page();
    u64 frame_phys = pmm_alloc_page();
    if (!pml4_phys || !frame_phys) {
        if (pml4_phys) pmm_free_page(pml4_phys);
        if (frame_phys) pmm_free_page(frame_phys);
        return false;
    }

    u64* pml4 = (u64*)vmm_phys_to_virt(pml4_phys);
    if (!pml4) {
        pmm_free_page(frame_phys);
        pmm_free_page(pml4_phys);
        return false;
    }
    kmemset(pml4, 0, PAGE_SIZE);

    struct mapping_case {
        u64 virt;
        u64 phys;
        u64 flags;
    };
    const struct mapping_case cases[] = {
        { 0x0000800000000000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x0000800000001000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0xFFFF000000000000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0xFFFF7FFFFFFFF000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x0000400000004001ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { UINT64_MAX, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x0000400000004000ULL, frame_phys + 1, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x0000400000004000ULL, 0x0010000000000000ULL, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x0000400000004000ULL, UINT64_MAX, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0xFFFF800000000000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0xFFFF900000001000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0xFFFFFFFFFFFFF000ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x0000400000004000ULL, 0x000FFFFFFFFFF001ULL, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0xFFFF7FFFFFFFF001ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER },
        { 0x00007FFFFFFFF001ULL, frame_phys, VMM_FLAG_PRESENT | VMM_FLAG_USER }
    };

    bool rejected = true;
    for (usize i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        u32 slot = (u32)((cases[i].virt >> 39) & 0x1FF);
        u64 before = pml4[slot];
        if (vmm_map_page_pml4((pml4_t*)pml4_phys, cases[i].virt,
                              cases[i].phys, cases[i].flags) ||
            pml4[slot] != before) {
            rejected = false;
            break;
        }
    }

    /* The current-address-space entry point must reject invalid requests
     * before touching its shared kernel page tables as well. */
    u64 current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    u64* current_pml4 = (u64*)vmm_phys_to_virt(current_cr3 & ~0xFFFULL);
    bool current_rejected = current_pml4 != NULL;
    if (current_rejected) {
        const u64 invalid_virtuals[] = {
            0x0000800000000000ULL,
            0xFFFF000000000000ULL,
            0xFFFF900000001000ULL
        };
        for (usize i = 0; i < sizeof(invalid_virtuals) / sizeof(invalid_virtuals[0]); i++) {
            u32 slot = (u32)((invalid_virtuals[i] >> 39) & 0x1FF);
            u64 before = current_pml4[slot];
            u64 flags = (i == 2)
                ? VMM_FLAG_PRESENT | VMM_FLAG_USER
                : VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
            if (vmm_map_page(invalid_virtuals[i], frame_phys, flags) ||
                current_pml4[slot] != before) {
                current_rejected = false;
                break;
            }
        }
    }

    pmm_free_page(frame_phys);
    pmm_free_page(pml4_phys);
    return rejected && current_rejected;
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
    report("user_kernel_half_denied", test_user_mapping_rejects_kernel_half());
    report("unaligned_page_denied", test_vmm_rejects_unaligned_pages());
    report("mapping_validation_matrix", test_mapping_validation_matrix());
}

