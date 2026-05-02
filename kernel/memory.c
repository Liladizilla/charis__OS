#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <kernel/types.h>

/* Multiboot info pointer */
extern u32 mb_magic;
extern u32 mb_info;

/* Physical memory manager (bitmap-based) */
static u8* pmm_bitmap = NULL;
static u64 pmm_bitmap_size = 0;
u64 pmm_total_frames = 0;
u64 pmm_used_frames = 0;
static u64 pmm_base_addr = 0;

/* Kernel heap */
static void* heap_start = NULL;
static usize heap_size = 0;
static void* heap_current = NULL;

/* Page table pointers (PML4, PDPT, PD, PT) - kept for compatibility */
static u64* pml4 = (u64*)0x1000;  /* PML4 at 4KB */
static u64* pdpt = (u64*)0x2000;  /* PDPT at 8KB */
static u64* pd = (u64*)0x3000;    /* PD at 12KB */

/* Allocation tracking for krealloc and kfree */
typedef struct alloc_header {
    usize size;
    usize magic;
    struct alloc_header* next;
    struct alloc_header* prev;
} alloc_header_t;

#define ALLOC_MAGIC 0xDEADBEEFDEADBEEF
#define ALLOC_FREE  0xFEEEFEEEFEEEFEEE
#define ALLOC_USED  0xDEADBEEFDEADBEEF

/* Free list for heap blocks */
static alloc_header_t* free_list = NULL;

void pmm_init(u64 total_mem, mem_map_entry_t* entries, u32 num_entries) {
    pmm_total_frames = total_mem / PAGE_SIZE;
    pmm_bitmap_size = (pmm_total_frames + 7) / 8;
    pmm_base_addr = 0x100000; // 1MB

    vga_printf("PMM: total_mem=%llu MB, frames=%llu, bitmap_size=%llu bytes\n",
               total_mem / (1024*1024), pmm_total_frames, pmm_bitmap_size);

    /* Allocate bitmap right after kernel */
    extern u8 _kernel_end[];
    pmm_bitmap = (u8*)ALIGN_UP((u64)_kernel_end, PAGE_SIZE);
    vga_printf("PMM: bitmap at 0x%llx\n", (u64)pmm_bitmap);

    /* Mark all frames as used initially */
    kmemset(pmm_bitmap, 0xFF, pmm_bitmap_size);
    pmm_used_frames = pmm_total_frames;

    /* Parse multiboot2 memory map and mark available regions as free */
    u32 mmap_entries = 0;
    u32 available_frames = 0;
    for (u32 i = 0; i < num_entries; i++) {
        mem_map_entry_t* entry = &entries[i];
        if (entry->type == MEM_AVAILABLE && entry->base >= 0x100000) {
            u64 start = entry->base;
            u64 end = entry->base + entry->length;
            /* Align start up to page boundary */
            if (start % PAGE_SIZE != 0) {
                start = ALIGN_UP(start, PAGE_SIZE);
            }
            /* Align end down to page boundary */
            end = ALIGN_DOWN(end, PAGE_SIZE);
            if (end > start) {
                u64 first_frame = (start - pmm_base_addr) / PAGE_SIZE;
                u64 num_frames = (end - start) / PAGE_SIZE;
                for (u64 f = 0; f < num_frames; f++) {
                    u64 frame = first_frame + f;
                    if (frame < pmm_total_frames) {
                        u64 byte = frame / 8;
                        u8 bit = frame % 8;
                        if (pmm_bitmap[byte] & (1 << bit)) {
                            pmm_bitmap[byte] &= ~(1 << bit);
                            pmm_used_frames--;
                            available_frames++;
                        }
                    }
                }
                mmap_entries++;
            }
        }
    }
    vga_printf("PMM: parsed %u memory map entries, %u available regions, %llu frames available\n",
               num_entries, mmap_entries, available_frames);
    vga_printf("PMM: used frames: %llu, free frames: %llu\n",
               pmm_used_frames, pmm_total_frames - pmm_used_frames);
}

/* Wrapper for compatibility - parses multiboot2 info and calls pmm_init */
void memory_init(u32 info) {
    u32 magic = mb_magic;
    u32 mboot_info = mb_info;
    
    vga_printf("Memory init: magic=0x%x, info=0x%x\n", magic, mboot_info);
    
    if (magic != 0x36d76289) {
        vga_printf("ERROR: Invalid multiboot2 magic!\n");
        /* Fallback: assume 128MB */
        pmm_init(128 * 1024 * 1024, NULL, 0);
        heap_init((void*)0x200000, 64 * 1024 * 1024);
        vmm_init();
        return;
    }
    
    /* Parse multiboot2 info */
    u32 total_mem = 0;
    u32 mmap_count = 0;
    mem_map_entry_t mmap_entries[32]; // Max 32 entries
    
    u8* mboot_ptr = (u8*)(u64)mboot_info;
    u32 total_size = *(u32*)mboot_ptr;
    u8* tag_ptr = mboot_ptr + 8; // Skip total_size and reserved
    
    while (1) {
        u32 tag_type = *(u32*)tag_ptr;
        u32 tag_size = *(u32*)(tag_ptr + 4);
        
        if (tag_type == 0) break; // End tag
        
        if (tag_type == 6) { // Memory map tag
            u32 entry_size = *(u32*)(tag_ptr + 8);
            u32 entry_version = *(u32*)(tag_ptr + 12);
            u8* entries_ptr = tag_ptr + 16;
            u32 num_entries = (entry_size - 16) / 24; // Each entry is 24 bytes
            
            for (u32 i = 0; i < num_entries && mmap_count < 32; i++) {
                mem_map_entry_t* entry = (mem_map_entry_t*)(entries_ptr + i * 24);
                mmap_entries[mmap_count++] = *entry;
                if (entry->type == MEM_AVAILABLE) {
                    u64 end = entry->base + entry->length;
                    if (end > total_mem) {
                        total_mem = (u32)(end > 0xFFFFFFFF ? 0xFFFFFFFF : end);
                    }
                }
            }
        }
        
        tag_ptr += (tag_size + 7) & ~7; // Align to 8 bytes
        if (tag_ptr >= mboot_ptr + total_size) break;
    }
    
    if (total_mem == 0) {
        total_mem = 128 * 1024 * 1024; // Fallback
    }
    
    vga_printf("MB2: total_mem=%u MB, mmap_entries=%u\n", total_mem / (1024*1024), mmap_count);
    
    pmm_init(total_mem, mmap_entries, mmap_count);
    heap_init((void*)0x200000, 64 * 1024 * 1024);
    vmm_init();
}

void* pmm_alloc_frame(void) {
    for (u64 i = 0; i < pmm_total_frames; i++) {
        u64 byte = i / 8;
        u8 bit = i % 8;
        if ((pmm_bitmap[byte] & (1 << bit)) == 0) {
            pmm_bitmap[byte] |= (1 << bit);
            pmm_used_frames++;
            return (void*)(pmm_base_addr + i * PAGE_SIZE);
        }
    }
    return NULL; // Out of memory
}

void pmm_free_frame(void* addr) {
    u64 frame = ((u64)addr - pmm_base_addr) / PAGE_SIZE;
    if (frame >= pmm_total_frames) return;
    u64 byte = frame / 8;
    u8 bit = frame % 8;
    if (pmm_bitmap[byte] & (1 << bit)) {
        pmm_bitmap[byte] &= ~(1 << bit);
        pmm_used_frames--;
    }
}

void heap_init(void* start, usize size) {
    /* Initialize the heap with one large free block */
    alloc_header_t* header = (alloc_header_t*)start;
    header->size = size - sizeof(alloc_header_t);
    header->magic = ALLOC_FREE;
    header->next = NULL;
    header->prev = NULL;
    
    free_list = header;
    
    heap_start = start;
    heap_size = size;
    heap_current = start;
    
    vga_printf("Heap: initialized at 0x%llx, size=%llu bytes\n", (u64)start, size);
}

void* kmalloc(usize size) {
    if (size == 0) return NULL;
    
    /* Align size to 16 bytes */
    size = ALIGN_UP(size, 16);
    
    /* Find a free block that fits */
    alloc_header_t* current = free_list;
    alloc_header_t* best_fit = NULL;
    usize best_fit_size = 0;
    
    while (current) {
        if (current->magic == ALLOC_FREE && current->size >= size + sizeof(alloc_header_t)) {
            if (!best_fit || current->size < best_fit_size) {
                best_fit = current;
                best_fit_size = current->size;
            }
        }
        current = current->next;
    }
    
    if (!best_fit) {
        /* No suitable free block, try bump allocation */
        if (heap_current == NULL) {
            return NULL;
        }
        void* ptr = heap_current;
        heap_current = (u8*)heap_current + size + sizeof(alloc_header_t);
        if ((u64)heap_current > (u64)heap_start + heap_size) {
            heap_current = ptr; // Roll back
            return NULL;
        }
        alloc_header_t* header = (alloc_header_t*)ptr;
        header->size = size;
        header->magic = ALLOC_USED;
        header->next = NULL;
        header->prev = NULL;
        return (void*)((u8*)ptr + sizeof(alloc_header_t));
    }
    
    /* Split the block if there's enough space for another allocation */
    if (best_fit_size >= sizeof(alloc_header_t) + size + sizeof(alloc_header_t) + 16) {
        /* Create a new header for the remaining space */
        alloc_header_t* new_block = (alloc_header_t*)((u8*)best_fit + sizeof(alloc_header_t) + size);
        new_block->size = best_fit_size - sizeof(alloc_header_t) - size;
        new_block->magic = ALLOC_FREE;
        new_block->next = best_fit->next;
        new_block->prev = best_fit;
        
        if (best_fit->next) {
            best_fit->next->prev = new_block;
        }
        best_fit->next = new_block;
        
        /* Update the current block size */
        best_fit->size = size;
    }
    
    /* Mark block as used */
    best_fit->magic = ALLOC_USED;
    
    /* Remove from free list */
    if (best_fit->prev) {
        best_fit->prev->next = best_fit->next;
    } else {
        free_list = best_fit->next;
    }
    if (best_fit->next) {
        best_fit->next->prev = best_fit->prev;
    }
    best_fit->next = NULL;
    best_fit->prev = NULL;
    
    /* Return pointer to usable memory */
    return (void*)((u8*)best_fit + sizeof(alloc_header_t));
}

void* krealloc(void* ptr, usize size) {
    if (!ptr) {
        return kmalloc(size);
    }
    
    /* Get header and verify magic */
    alloc_header_t* header = (alloc_header_t*)((u8*)ptr - sizeof(alloc_header_t));
    if (header->magic != ALLOC_USED) {
        return NULL; /* Invalid pointer */
    }
    
    /* If new size is smaller or equal, just return the same pointer */
    if (header->size >= size) {
        return ptr;
    }
    
    /* Allocate new block */
    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        /* Copy old data */
        kmemcpy(new_ptr, ptr, header->size);
        /* Free old block */
        kfree(ptr);
    }
    return new_ptr;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    /* Get header and verify magic */
    alloc_header_t* header = (alloc_header_t*)((u8*)ptr - sizeof(alloc_header_t));
    if (header->magic != ALLOC_USED) {
        return; /* Invalid pointer */
    }
    
    /* Mark block as free */
    header->magic = ALLOC_FREE;
    
    /* Add to free list */
    header->next = free_list;
    header->prev = NULL;
    if (free_list) {
        free_list->prev = header;
    }
    free_list = header;
    
    /* Try to coalesce with next block */
    alloc_header_t* next = (alloc_header_t*)((u8*)header + sizeof(alloc_header_t) + header->size);
    if ((u64)next < (u64)heap_start + heap_size && next->magic == ALLOC_FREE) {
        header->size += sizeof(alloc_header_t) + next->size;
        /* Remove next from free list */
        if (next->prev) {
            next->prev->next = next->next;
        } else {
            free_list = next->next;
        }
        if (next->next) {
            next->next->prev = next->prev;
        }
    }
    
    /* Try to coalesce with previous block */
    /* We'd need to track all blocks to find previous, skip for now */
}

void vmm_init(void) {
    /* Page tables are already set up by bootloader for identity mapping.
     * In a full implementation, we would set up per-process page tables. */
    vga_puts("VMM: Using boot loader page tables\n");
}

void* vmm_alloc_page(void) {
    void* phys = pmm_alloc_frame();
    if (!phys) return NULL;
    
    /* For now, just return physical address (identity mapped) */
    return phys;
}

void vmm_free_page(void* virt) {
    void* phys = vmm_get_phys(virt);
    pmm_free_frame(phys);
    vmm_unmap_page(virt);
}

void vmm_map_page(void* phys, void* virt, u64 flags) {
    /* Calculate page table indices */
    u64 vaddr = (u64)virt;
    u64 paddr = (u64)phys;
    
    u64 pml4_idx = (vaddr >> 39) & 0x1FF;
    u64 pdpt_idx = (vaddr >> 30) & 0x1FF;
    u64 pd_idx = (vaddr >> 21) & 0x1FF;
    u64 pt_idx = (vaddr >> 12) & 0x1FF;
    (void)pml4_idx; (void)pdpt_idx; (void)pt_idx; // Unused for now
    
    /* For simplicity, use 2MB huge pages (direct mapping) */
    u64 pd_entry = paddr | flags | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE;
    
    /* Store in page directory (simplified - direct mapping) */
    pd[pd_idx] = pd_entry;
    
    /* Invalidate TLB for this page */
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_unmap_page(void* virt) {
    /* Clear the page table entry */
    u64 vaddr = (u64)virt;
    u64 pd_idx = (vaddr >> 21) & 0x1FF;
    
    pd[pd_idx] = 0;
    
    /* Invalidate TLB */
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void* vmm_get_phys(void* virt) {
    /* For identity-mapped kernel, virtual == physical */
    return virt;
}