#include <kernel/elf.h>
#include <kernel/vfs.h>
#include <kernel/memory.h>
#include <kernel/pmm.h>
#include <kernel/vga.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/vmm.h>
#include <kernel/security.h>
#include <kernel/scheduler.h>

#define ELF_MAX_VECTOR_ENTRIES 16
#define ELF_MAX_SEGMENT_BYTES (32ULL * 1024ULL * 1024ULL)
#define ELF_MAX_IMAGE_BYTES   (64ULL * 1024ULL * 1024ULL)
#define ELF_USER_IMAGE_LIMIT  0x0000700000000000ULL

static bool elf_write_user(pml4_t* pml4, u64 address, const void* source, usize size) {
    usize copied = 0;
    while (copied < size) {
        u64 current = address + copied;
        pte_t* pte = vmm_walk(pml4, current, false);
        if (!pte || !(*pte & VMM_FLAG_PRESENT)) return false;
        u64 phys = (*pte & 0x000FFFFFFFFFF000ULL) | (current & (PAGE_SIZE - 1));
        usize chunk = PAGE_SIZE - (usize)(current & (PAGE_SIZE - 1));
        if (chunk > size - copied) chunk = size - copied;
        void* destination_page = vmm_phys_to_virt(phys);
        if (!destination_page) return false;
        kmemcpy(destination_page, (const u8*)source + copied, chunk);
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
    if (phdr->p_memsz > ELF_MAX_SEGMENT_BYTES || phdr->p_vaddr < PAGE_SIZE ||
        phdr->p_filesz > phdr->p_memsz ||
        phdr->p_vaddr >= ELF_USER_IMAGE_LIMIT ||
        phdr->p_memsz > ELF_USER_IMAGE_LIMIT - phdr->p_vaddr ||
        (phdr->p_flags & (PF_W | PF_X)) == (PF_W | PF_X)) {
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
        void* page_alias = vmm_phys_to_virt(phys);
        if (!page_alias) { pmm_free_page(phys); return false; }
        kmemset(page_alias, 0, PAGE_SIZE);
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
            u64 phys = (*pte & 0x000FFFFFFFFFF000ULL) | (vaddr & (PAGE_SIZE - 1));
            usize page_bytes = PAGE_SIZE - (usize)(vaddr & (PAGE_SIZE - 1));
            usize bytes = chunk - written;
            if (bytes > page_bytes) bytes = page_bytes;
            void* destination_page = vmm_phys_to_virt(phys);
            if (!destination_page) return false;
            kmemcpy(destination_page, buffer + written, bytes);
            written += bytes;
        }
        copied += chunk;
    }

    return true;
}

static void elf_rollback_segments(pml4_t* pml4,
                                  const elf64_phdr_t* phdrs, u32 count) {
    if (!pml4 || !phdrs) return;
    for (u32 i = 0; i < count; i++) {
        const elf64_phdr_t* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD || !phdr->p_memsz) continue;
        u64 start = phdr->p_vaddr & ~(PAGE_SIZE - 1);
        u64 end = phdr->p_vaddr + phdr->p_memsz;
        end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (u64 address = start; address < end; address += PAGE_SIZE) {
            u64 phys = vmm_unmap_page_pml4(pml4, address);
            if (phys) pmm_free_page(phys);
        }
    }
}

static bool elf_validate_segments(pml4_t* pml4, const elf64_phdr_t* phdrs,
                                  u32 count, u64 file_size) {
    u64 total_memory = 0;
    for (u32 i = 0; i < count; i++) {
        const elf64_phdr_t* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;
        if (!phdr->p_memsz) {
            if (phdr->p_filesz) return false;
            continue;
        }
        if (phdr->p_memsz > ELF_MAX_SEGMENT_BYTES ||
            phdr->p_filesz > phdr->p_memsz ||
            phdr->p_vaddr < PAGE_SIZE ||
            phdr->p_vaddr >= ELF_USER_IMAGE_LIMIT ||
            phdr->p_memsz > ELF_USER_IMAGE_LIMIT - phdr->p_vaddr ||
            phdr->p_offset > file_size ||
            phdr->p_filesz > file_size - phdr->p_offset ||
            (phdr->p_flags & ~(PF_R | PF_W | PF_X)) ||
            (phdr->p_flags & (PF_W | PF_X)) == (PF_W | PF_X)) return false;

        if (phdr->p_align > 1 &&
            ((phdr->p_align & (phdr->p_align - 1)) ||
             (phdr->p_vaddr & (phdr->p_align - 1)) !=
             (phdr->p_offset & (phdr->p_align - 1)))) return false;

        if (total_memory > ELF_MAX_IMAGE_BYTES - phdr->p_memsz) return false;
        total_memory += phdr->p_memsz;

        u64 start = phdr->p_vaddr & ~(PAGE_SIZE - 1);
        u64 segment_end = phdr->p_vaddr + phdr->p_memsz;
        if (segment_end > UINT64_MAX - (PAGE_SIZE - 1)) return false;
        u64 end = (segment_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (end <= start || end > ELF_USER_IMAGE_LIMIT) return false;

        /* Reject overlap at page granularity. Two PT_LOAD segments sharing
         * a page cannot safely have independent W/X permissions. */
        for (u32 j = 0; j < i; j++) {
            const elf64_phdr_t* previous = &phdrs[j];
            if (previous->p_type != PT_LOAD || !previous->p_memsz) continue;
            u64 previous_start = previous->p_vaddr & ~(PAGE_SIZE - 1);
            u64 previous_end = previous->p_vaddr + previous->p_memsz;
            previous_end = (previous_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (start < previous_end && previous_start < end) return false;
        }

        /* Do not replace bootstrap code, stack, or any other existing user
         * mapping and accidentally leak its frame. */
        for (u64 address = start; address < end; address += PAGE_SIZE) {
            pte_t* pte = vmm_walk(pml4, address, false);
            if (pte && (*pte & VMM_FLAG_PRESENT)) return false;
        }
    }
    return total_memory != 0;
}

static int elf_load_internal(const char* path, process_mm_t* mm, u64* entry_point,
                             u64* user_rsp, const char* const argv[], int argc,
                             const char* const envp[], int envc,
                             bool setup_initial_stack) {
    if (!path || !mm || !mm->pml4 || !entry_point ||
        (setup_initial_stack && !user_rsp)) return -1;

    /* Keep policy at the loader boundary too; callers must not be able to
     * bypass syscall path checks or execution capabilities. */
    task_t* current = scheduler_current();
    if (!security_verify_path(path, current)) return -1;
    if (current && current->is_user &&
        (!security_check_capability(current, CAP_SPAWN) ||
         !security_check_capability(current, CAP_ALLOW_EXEC))) return -1;

    vfs_node_t* node = vfs_resolve(path);
    if (!node || !(node->flags & VFS_FILE) || !node->read) return -1;

    int fd = fd_alloc(node, 0);
    if (fd < 0) return -1;

    int result = -1;
    bool load_started = false;
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

    if (!elf_validate_segments(mm->pml4, phdrs, ehdr.e_phnum, node->size)) goto done;

    bool entry_is_executable = false;
    for (u32 i = 0; i < ehdr.e_phnum; i++) {
        elf64_phdr_t* phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;
        load_started = true;
        if (!elf_load_segment(mm->pml4, fd, phdr)) goto done;
        if ((phdr->p_flags & PF_X) && ehdr.e_entry >= phdr->p_vaddr &&
            ehdr.e_entry - phdr->p_vaddr < phdr->p_memsz) {
            entry_is_executable = true;
        }
    }

    if (!entry_is_executable) goto done;
    /*
     * Treat PT_LOAD mappings and the initial user stack as one exec
     * transaction. elf_setup_stack() rolls back its own partial stack pages;
     * if it cannot construct the stack, the common failure path below also
     * removes every segment mapped by this load.
     */
    if (setup_initial_stack &&
        elf_setup_stack(mm, user_rsp, argv, argc, envp, envc) < 0) goto done;
    *entry_point = ehdr.e_entry;
    result = 0;

done:
    if (result < 0 && load_started && phdrs)
        elf_rollback_segments(mm->pml4, phdrs, ehdr.e_phnum);
    if (phdrs) kfree(phdrs);
    fd_close(fd);
    if (result < 0) vga_puts("ELF: invalid or unreadable executable\n");
    return result;
}

int elf_load(const char* path, process_mm_t* mm, u64* entry_point) {
    return elf_load_internal(path, mm, entry_point, NULL, NULL, 0,
                             NULL, 0, false);
}

int elf_load_exec(const char* path, process_mm_t* mm, u64* entry_point,
                  u64* user_rsp, const char* const argv[], int argc,
                  const char* const envp[], int envc) {
    return elf_load_internal(path, mm, entry_point, user_rsp, argv, argc,
                             envp, envc, true);
}

static void elf_rollback_stack(pml4_t* pml4, u64 stack_base, u32 mapped_pages) {
    for (u32 i = 0; i < mapped_pages; i++) {
        u64 phys = vmm_unmap_page_pml4(pml4, stack_base + (u64)i * PAGE_SIZE);
        if (phys) pmm_free_page(phys);
    }
}

int elf_setup_stack(process_mm_t* mm, u64* user_rsp,
                  const char* const argv[], int argc,
                  const char* const envp[], int envc) {
    if (!mm || !mm->pml4 || !user_rsp || argc < 0 || envc < 0 ||
        argc > ELF_MAX_VECTOR_ENTRIES || envc > ELF_MAX_VECTOR_ENTRIES ||
        (argc && !argv) || (envc && !envp)) return -1;
    /*
     * The bootstrap ring-3 task maps aliases of its kernel stack near the
     * conventional top of user memory. exec() reuses that address space, so
     * blindly insisting on the top two pages makes the first ELF exec fail.
     * Find a free two-page window below the usual stack top instead of
     * replacing mappings whose physical pages are still owned by the kernel.
     */
    u64 stack_top = 0x00007FFFFFFFE000ULL;
    u64 stack_base = 0;
    bool found_stack = false;
    for (u32 attempt = 0; attempt < 4096; attempt++) {
        if (stack_top < 2 * PAGE_SIZE) break;
        u64 candidate_base = stack_top - 2 * PAGE_SIZE;
        bool occupied = false;
        for (u64 addr = candidate_base; addr < stack_top; addr += PAGE_SIZE) {
            pte_t* existing = vmm_walk(mm->pml4, addr, false);
            if (existing && (*existing & VMM_FLAG_PRESENT)) {
                occupied = true;
                break;
            }
        }
        if (!occupied) {
            stack_base = candidate_base;
            found_stack = true;
            break;
        }
        stack_top -= PAGE_SIZE;
    }
    if (!found_stack) return -1;

    u32 mapped_pages = 0;
    for (u64 addr = stack_base; addr < stack_top; addr += PAGE_SIZE) {
        u64 phys = pmm_alloc_page();
        if (!phys) goto fail;
        void* page_alias = vmm_phys_to_virt(phys);
        if (!page_alias) { pmm_free_page(phys); goto fail; }
        kmemset(page_alias, 0, PAGE_SIZE);
        if (!vmm_map_page_pml4(mm->pml4, addr, phys,
                VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER | VMM_FLAG_NX)) {
            pmm_free_page(phys);
            goto fail;
        }
        mapped_pages++;
    }

    u64 argv_user[ELF_MAX_VECTOR_ENTRIES];
    u64 envp_user[ELF_MAX_VECTOR_ENTRIES];
    u64 cursor = stack_top;
    for (int i = envc - 1; i >= 0; i--) {
        if (!envp[i]) goto fail;
        usize length = kstrlen(envp[i]) + 1;
        if (length > cursor - stack_base) goto fail;
        cursor -= length;
        if (!elf_write_user(mm->pml4, cursor, envp[i], length)) goto fail;
        envp_user[i] = cursor;
    }
    for (int i = argc - 1; i >= 0; i--) {
        if (!argv[i]) goto fail;
        usize length = kstrlen(argv[i]) + 1;
        if (length > cursor - stack_base) goto fail;
        cursor -= length;
        if (!elf_write_user(mm->pml4, cursor, argv[i], length)) goto fail;
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
    if (vector_size > cursor - stack_base) goto fail;
    u64 vector_address = (cursor - vector_size) & ~0xFULL;
    if (vector_address < stack_base ||
        !elf_write_user(mm->pml4, vector_address, words, vector_size)) goto fail;

    *user_rsp = vector_address;
    mm->stack_top = vector_address;
    return 0;

fail:
    elf_rollback_stack(mm->pml4, stack_base, mapped_pages);
    return -1;
}

int elf_mmap_segment(process_mm_t* mm, u64 vaddr, u64 filesz, u64 memsz, u64 flags) {
    (void)mm; (void)vaddr; (void)filesz; (void)memsz; (void)flags;
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