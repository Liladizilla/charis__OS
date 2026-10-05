#include <kernel/elf.h>
#include <kernel/vfs.h>
#include <kernel/memory.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/printf.h>

#define ELF_MAX_VECTOR_ENTRIES 16

static bool elf_write_user(pml4_t* pml4, u64 address, const void* source, usize size) {
    usize copied = 0;
    while (copied < size) {
        u64 current = address + copied;
        pte_t* pte = vmm_walk(pml4, current, false);
        if (!pte || !(*pte & VMM_FLAG_PRESENT)) return false;
        u64 phys = (*pte & ~0xFFFULL) | (current & (PAGE_SIZE - 1));
        usize chunk = PAGE_SIZE - (usize)(current & (PAGE_SIZE - 1));
        if (chunk > size - copied) chunk = size - copied;
        kmemcpy((void*)(uintptr_t)phys, (const u8*)source + copied, chunk);
        copied += chunk;
    }
    return true;
}

static int fd_read_all(int fd, u64 offset, void* buf, usize count) {
    fd_entry_t* f = fd_get(fd);
    if (!f || !f->node || !f->node->read) return -1;
    usize total = 0;
    while (total < count) {
        int read = f->node->read(f->node, offset + total, count - total,
                                 (u8*)buf + total);
        if (read <= 0) return -1;
        total += (usize)read;
    }
    return (int)total;
}

static bool elf_load_segment(pml4_t* pml4, int fd, const elf64_phdr_t* phdr) {
    if (!phdr->p_memsz) return phdr->p_filesz == 0;
    if (phdr->p_memsz > 0x0000800000000000ULL || phdr->p_vaddr < PAGE_SIZE ||
        phdr->p_filesz > phdr->p_memsz ||
        phdr->p_vaddr > 0x0000800000000000ULL - phdr->p_memsz) {
        return false;
    }

    u64 segment_end = phdr->p_vaddr + phdr->p_memsz;
    u64 page_start = phdr->p_vaddr & ~(PAGE_SIZE - 1);
    u64 page_end = (segment_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (page_end < segment_end) return false;

    u64 map_flags = VMM_FLAG_PRESENT | VMM_FLAG_USER;
    if (phdr->p_flags & PF_W) map_flags |= VMM_FLAG_WRITABLE;
    if (!(phdr->p_flags & PF_X)) map_flags |= VMM_FLAG_NX;

    for (u64 addr = page_start; addr < page_end; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) return false;
        kmemset((void*)(uintptr_t)phys, 0, PAGE_SIZE);
        if (!vmm_map_page_pml4(pml4, addr, phys, map_flags)) {
            pmm_free_page(phys);
            return false;
        }
    }

    u8 buffer[512];
    u64 copied = 0;
    while (copied < phdr->p_filesz) {
        usize chunk = (usize)(phdr->p_filesz - copied);
        if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
        if (fd_read_all(fd, phdr->p_offset + copied, buffer, chunk) != (int)chunk) {
            return false;
        }

        usize written = 0;
        while (written < chunk) {
            u64 vaddr = phdr->p_vaddr + copied + written;
            pte_t* pte = vmm_walk(pml4, vaddr, false);
            if (!pte || !(*pte & VMM_FLAG_PRESENT)) return false;
            u64 phys = (*pte & ~0xFFFULL) | (vaddr & (PAGE_SIZE - 1));
            usize page_bytes = PAGE_SIZE - (usize)(vaddr & (PAGE_SIZE - 1));
            usize bytes = chunk - written;
            if (bytes > page_bytes) bytes = page_bytes;
            kmemcpy((void*)(uintptr_t)phys, buffer + written, bytes);
            written += bytes;
        }
        copied += chunk;
    }

    return true;
}

int elf_load(const char* path, process_mm_t* mm, u64* entry_point) {
    if (!path || !mm || !mm->pml4 || !entry_point) return -1;

    vfs_node_t* node = vfs_resolve(path);
    if (!node || !(node->flags & VFS_FILE) || !node->read) return -1;

    int fd = fd_alloc(node, 0);
    if (fd < 0) return -1;

    int result = -1;
    elf64_phdr_t* phdrs = NULL;
    elf64_ehdr_t ehdr;
    if (fd_read_all(fd, 0, &ehdr, sizeof(ehdr)) != (int)sizeof(ehdr)) goto done;

    if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F' ||
        ehdr.e_ident[4] != 2 || ehdr.e_ident[5] != 1 || ehdr.e_ident[6] != 1 ||
        ehdr.e_type != ET_EXEC || ehdr.e_machine != EM_X86_64 ||
        ehdr.e_version != 1 || ehdr.e_ehsize != sizeof(ehdr) ||
        ehdr.e_phentsize != sizeof(elf64_phdr_t) || !ehdr.e_phnum ||
        ehdr.e_phnum > 128 || !ehdr.e_phoff) goto done;

    u64 phdr_bytes = (u64)ehdr.e_phnum * sizeof(elf64_phdr_t);
    if (ehdr.e_phoff > node->size || phdr_bytes > node->size - ehdr.e_phoff) goto done;

    phdrs = (elf64_phdr_t*)kmalloc((usize)phdr_bytes);
    if (!phdrs || fd_read_all(fd, ehdr.e_phoff, phdrs, (usize)phdr_bytes) !=
                  (int)phdr_bytes) goto done;

    bool entry_is_executable = false;
    bool loaded_segment = false;
    for (u32 i = 0; i < ehdr.e_phnum; i++) {
        elf64_phdr_t* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;
        if (phdr->p_offset > node->size || phdr->p_filesz > node->size - phdr->p_offset) goto done;
        if (phdr->p_align > 1 &&
            ((phdr->p_align & (phdr->p_align - 1)) ||
             (phdr->p_vaddr & (phdr->p_align - 1)) != (phdr->p_offset & (phdr->p_align - 1)))) goto done;
        if (!elf_load_segment(mm->pml4, fd, phdr)) goto done;
        loaded_segment = true;
        if ((phdr->p_flags & PF_X) && ehdr.e_entry >= phdr->p_vaddr &&
            ehdr.e_entry - phdr->p_vaddr < phdr->p_memsz) {
            entry_is_executable = true;
        }
    }

    if (!loaded_segment || !entry_is_executable) {
        goto done;
    }
    *entry_point = ehdr.e_entry;
    result = 0;

done:
    if (phdrs) kfree(phdrs);
    fd_close(fd);
    if (result < 0) vga_puts("ELF: invalid or unreadable executable\n");
    return result;
}

int elf_setup_stack(process_mm_t* mm, u64* user_rsp,
                  const char* const argv[], int argc,
                  const char* const envp[], int envc) {
    if (!mm || !mm->pml4 || !user_rsp || argc < 0 || envc < 0 ||
        argc > ELF_MAX_VECTOR_ENTRIES || envc > ELF_MAX_VECTOR_ENTRIES ||
        (argc && !argv) || (envc && !envp)) return -1;
    const u64 stack_top = 0x00007FFFFFFFE000ULL;
    const u64 stack_base = stack_top - 2 * PAGE_SIZE;
    for (u64 addr = stack_base; addr < stack_top; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) return -1;
        kmemset((void*)(uintptr_t)phys, 0, PAGE_SIZE);
        if (!vmm_map_page_pml4(mm->pml4, addr, phys,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)) {
            pmm_free_page(phys);
            return -1;
        }
    }

    u64 argv_user[ELF_MAX_VECTOR_ENTRIES];
    u64 envp_user[ELF_MAX_VECTOR_ENTRIES];
    u64 cursor = stack_top;
    for (int i = envc - 1; i >= 0; i--) {
        if (!envp[i]) return -1;
        usize length = kstrlen(envp[i]) + 1;
        if (length > cursor - stack_base) return -1;
        cursor -= length;
        if (!elf_write_user(mm->pml4, cursor, envp[i], length)) return -1;
        envp_user[i] = cursor;
    }
    for (int i = argc - 1; i >= 0; i--) {
        if (!argv[i]) return -1;
        usize length = kstrlen(argv[i]) + 1;
        if (length > cursor - stack_base) return -1;
        cursor -= length;
        if (!elf_write_user(mm->pml4, cursor, argv[i], length)) return -1;
        argv_user[i] = cursor;
    }

    u64 words[1 + ELF_MAX_VECTOR_ENTRIES + 1 + ELF_MAX_VECTOR_ENTRIES + 1 + 2];
    usize word_count = 0;
    words[word_count++] = (u64)argc;
    for (int i = 0; i < argc; i++) words[word_count++] = argv_user[i];
    words[word_count++] = 0;
    for (int i = 0; i < envc; i++) words[word_count++] = envp_user[i];
    words[word_count++] = 0;
    words[word_count++] = 0; // AT_NULL
    words[word_count++] = 0;

    u64 vector_size = word_count * sizeof(u64);
    if (vector_size > cursor - stack_base) return -1;
    u64 vector_address = (cursor - vector_size) & ~0xFULL;
    if (vector_address < stack_base ||
        !elf_write_user(mm->pml4, vector_address, words, vector_size)) return -1;

    *user_rsp = vector_address;
    mm->stack_top = vector_address;
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