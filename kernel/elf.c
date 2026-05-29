#include <kernel/elf.h>
#include <kernel/vfs.h>
#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/string.h>

// Read from file descriptor into buffer
static int fd_read_all(int fd, u64 offset, void* buf, usize count) {
    fd_entry_t* f = fd_get(fd);
    if (!f || !f->node || !f->node->read) return -1;
    return f->node->read(f->node, offset, count, (u8*)buf);
}

// Allocate pages and map them
static bool elf_map_pages(pml4_t* pml4, u64 vaddr, u64 filesz, u64 memsz, u64 flags) {
    u64 map_flags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    if (flags & PF_X) map_flags |= VMM_FLAG_USER;
    if (flags & PF_W) map_flags |= VMM_FLAG_WRITABLE;
    if (flags & PF_R) map_flags |= VMM_FLAG_PRESENT;
    
    // Map pages for the segment
    for (u64 addr = vaddr; addr < vaddr + filesz; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) return false;
        vmm_map_page_pml4(pml4, addr, phys, map_flags);
    }
    
    // Zero the rest (BSS)
    for (u64 addr = vaddr + filesz; addr < vaddr + memsz; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) return false;
        vmm_map_page_pml4(pml4, addr, phys, map_flags);
        kmemset((void*)phys, 0, PAGE_SIZE);
    }
    
    return true;
}

int elf_load(const char* path, process_mm_t* mm, u64* entry_point) {
    // Open file
    vfs_node_t* node = vfs_resolve(path);
    if (!node) {
        vga_puts("ELF: File not found: ");
        vga_puts(path);
        return -1;
    }
    
    // Open and get fd
    int fd = fd_alloc(node, 0);
    if (fd < 0) return -1;
    
    // Read and parse ELF header
    elf64_ehdr_t ehdr;
    if (fd_read_all(fd, 0, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        vga_puts("ELF: Failed to read header\n");
        return -1;
    }
    
    // Verify ELF magic
    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' || 
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        vga_puts("ELF: Invalid magic\n");
        return -1;
    }
    
    // Check architecture
    if (ehdr.e_machine != EM_X86_64) {
        vga_puts("ELF: Not x86_64\n");
        return -1;
    }
    
    // Check type - only ET_EXEC for now
    if (ehdr.e_type != ET_EXEC) {
        vga_puts("ELF: Not executable\n");
        return -1;
    }
    
    *entry_point = ehdr.e_entry;
    
    // Process program headers
    elf64_phdr_t* phdrs = (elf64_phdr_t*)kmalloc(ehdr.e_phnum * sizeof(elf64_phdr_t));
    if (!phdrs) return -1;
    
    if (fd_read_all(fd, ehdr.e_phoff, phdrs, ehdr.e_phnum * sizeof(ehdr)) != 
        (int)(ehdr.e_phnum * sizeof(ehdr))) {
        kfree(phdrs);
        return -1;
    }
    
    // Map each PT_LOAD segment
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            elf_map_pages(mm->pml4, phdrs[i].p_vaddr, 
                        phdrs[i].p_filesz, phdrs[i].p_memsz,
                        phdrs[i].p_flags);
        }
    }
    
    kfree(phdrs);
    fd_close(fd);
    
    return 0;
}

int elf_setup_stack(process_mm_t* mm, u64* stack_top, 
                  const char* const argv[], int argc,
                  const char* const envp[], int envc) {
    // For now, just allocate stack pages
    // Full implementation would push argc, argv, envp, auxv
    u64 stack_base = 0x00007FFFFFFFF000ULL; // Typical user stack top
    *stack_top = stack_base;
    
    // Map stack pages (2 pages for now)
    for (u64 addr = stack_base - 8192; addr < stack_base; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) return -1;
        vmm_map_page_pml4(mm->pml4, addr, phys, 
                         VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER);
    }
    
    return 0;
}

int elf_mmap_segment(process_mm_t* mm, u64 vaddr, u64 filesz, u64 memsz, u64 flags) {
    return 0; // Handled in elf_load
}

int mm_brk(process_mm_t* mm, u64 new_end) {
    // Grow/shrink heap
    if (new_end < mm->heap_start) return -1;
    
    mm->heap_end = new_end;
    return 0;
}

u64 mm_sbrk(process_mm_t* mm) {
    return mm->heap_end;
}