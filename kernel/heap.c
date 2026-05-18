#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/vga.h>

// Simple best-fit free-list heap (split/merge)
// NOTE: This is moved out of the previous broken kernel/memory.c.

typedef struct heap_block {
    usize size;
    struct heap_block* next;
    bool free;
} heap_block_t;

static heap_block_t* heap_head = NULL;
static u64 heap_start = 0;
static usize heap_size = 0;

void heap_init(void* start, usize size) {
    heap_start = (u64)start;
    heap_size = size;

    heap_head = (heap_block_t*)start;
    heap_head->size = size - sizeof(heap_block_t);
    heap_head->next = NULL;
    heap_head->free = true;
}

void* kmalloc(usize size) {
    if (!heap_head || size == 0) return 0;

    // Basic alignment to 16 bytes
    usize aligned = (size + 15) & ~((usize)15);

    heap_block_t* curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= aligned + sizeof(heap_block_t)) {
            heap_block_t* new_block = (heap_block_t*)((u8*)curr + sizeof(heap_block_t) + aligned);
            new_block->size = curr->size - aligned - sizeof(heap_block_t);
            new_block->next = curr->next;
            new_block->free = true;

            curr->size = aligned;
            curr->next = new_block;
            curr->free = false;

            return (void*)((u8*)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    if (!heap_head) return;

    heap_block_t* block = (heap_block_t*)((u8*)ptr - sizeof(heap_block_t));
    block->free = true;

    // Merge with next
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
    }

    // Merge with previous (linear scan)
    heap_block_t* prev = heap_head;
    while (prev && prev->next != block) prev = prev->next;
    if (prev && prev->free) {
        prev->size += sizeof(heap_block_t) + block->size;
        prev->next = block->next;
    }
}

