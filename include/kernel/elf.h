/* elf.h - ELF executable loader */
#pragma once
#include <kernel/types.h>
#include <kernel/vmm.h>

#define EI_NIDENT   16
#define ELF_MAGIC   0x464C457FUL // "\x7FELF"

#define ET_EXEC     2
#define ET_DYN      3

#define EM_X86_64   62

#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_NOTE     4
#define PT_SHLIB    5
#define PT_PHDR     6

#define PF_X        (1 << 0)
#define PF_W        (1 << 1)
#define PF_R        (1 << 2)

typedef struct {
    u8 e_ident[EI_NIDENT];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed)) elf64_phdr_t;

// Load ELF from disk into process address space
int elf_load(const char* path, process_mm_t* mm, u64* entry_point);

// Set up initial stack for a new process
int elf_setup_stack(process_mm_t* mm, u64* stack_top, 
                  const char* const argv[], int argc,
                  const char* const envp[], int envc);

// Allocate memory for ELF segment
int elf_mmap_segment(process_mm_t* mm, u64 vaddr, u64 filesz, u64 memsz, u64 flags);

// Create a free memory region for brk/sbrk
int mm_brk(process_mm_t* mm, u64 new_end);

// Get current brk
u64 mm_sbrk(process_mm_t* mm);