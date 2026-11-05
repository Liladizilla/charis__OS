#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/memory.h>
#include <kernel/serial.h>
#include <kernel/vga.h>
#include <kernel/types.h>

static void report(const char* name, bool ok) {
    if (ok) {
        vga_puts("[VMM] ");
        vga_puts(name);
        vga_puts(": OK\n");
    } else {
        vga_puts("[VMM] ");
        vga_puts(name);
        vga_puts(": FAIL\n");
    }
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

    u64 virt = 0xFFFFA00000004000ULL;

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
    return (pte & (1ULL << 2)) != 0;
}

void vmm_run_tests(void) {
    report("map_existing", test_map_existing());
    report("map_missing", test_map_missing_page());
    report("remap_same", test_remap_same_page());
    report("unmap_page", test_unmap_page());
    report("map_user_page", test_map_user_page());
}

