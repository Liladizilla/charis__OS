#include <kernel/memory.h>
#include <kernel/vga.h>
#include <kernel/multiboot.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

#define HEAP_START 0xFFFF900000000000 // Higher half heap
#define HEAP_SIZE  (1024 * 1024 * 4)  // 4MB heap

typedef struct block {
    usize size;
    struct block* next;
    bool free;
} block_t;

static block_t* heap_head = NULL;

void memory_init(multiboot_info_t* info) {
    pmm_init(info);
    vmm_init();

    // Allocate heap pages
    for (u64 addr = HEAP_START; addr < HEAP_START + HEAP_SIZE; addr += 4096) {
        u64 phys = pmm_alloc_page();
        if (!phys) {
            vga_puts_error("ERROR: Failed to allocate heap page!");
            while (1) asm volatile("hlt");
        }
        vmm_map_page(addr, phys, PTE_WRITABLE);
    }

    // Initialize free list
    heap_head = (block_t*)HEAP_START;
    heap_head->size = HEAP_SIZE - sizeof(block_t);
    heap_head->next = NULL;
    heap_head->free = true;

    vga_puts("Memory initialized\n");
}

void* kmalloc(usize size) {
    block_t* curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size + sizeof(block_t)) {
            // Split block
            block_t* new_block = (block_t*)((u8*)curr + sizeof(block_t) + size);
            new_block->size = curr->size - size - sizeof(block_t);
            new_block->next = curr->next;
            new_block->free = true;

            curr->size = size;
            curr->next = new_block;
            curr->free = false;

            return (void*)((u8*)curr + sizeof(block_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_t* block = (block_t*)((u8*)ptr - sizeof(block_t));
    block->free = true;

    // Merge with next if free
    if (block->next && block->next->free) {
        block->size += sizeof(block_t) + block->next->size;
        block->next = block->next->next;
    }
    // Merge prev if free (scan for prev)
    block_t* prev = heap_head;
    while (prev && prev->next != block) prev = prev->next;
    if (prev && prev->free) {
        prev->size += sizeof(block_t) + block->size;
        prev->next = block->next;
    }
}