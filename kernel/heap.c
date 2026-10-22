#include <kernel/memory.h>
#include <kernel/string.h>
#include <kernel/vga.h>

// Simple best-fit free-list heap (split/merge)
// NOTE: This is moved out of the previous broken kernel/memory.c.

typedef struct heap_block {
    usize size;
    struct heap_block* next;
    struct heap_block* prev;
    bool free;
} heap_block_t;

static heap_block_t* heap_head = NULL;
static u64 heap_start = 0;
static usize heap_size = 0;

/* The allocator is shared with IRQ paths (for example, keyboard wakeups).
 * On this single-CPU kernel, mask maskable interrupts while mutating the
 * free list so an IRQ cannot re-enter kmalloc/kfree halfway through a split
 * or merge. This is not an SMP lock; a multi-core port needs a real
 * irq-saving spinlock. */
static inline u64 heap_irq_save(void) {
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void heap_irq_restore(u64 flags) {
    if (flags & (1ULL << 9)) {
        asm volatile("sti" : : : "memory");
    }
}

void heap_init(void* start, usize size) {
    heap_start = (u64)start;
    heap_size = size;

    heap_head = (heap_block_t*)start;
    heap_head->size = size - sizeof(heap_block_t);
    heap_head->next = NULL;
    heap_head->prev = NULL;
    heap_head->free = true;
}

void* kmalloc(usize size) {
    if (!heap_head || size == 0) return 0;

    u64 irq_flags = heap_irq_save();

    // Basic alignment to 16 bytes
    usize aligned = (size + 15) & ~((usize)15);

    heap_block_t* curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= aligned + sizeof(heap_block_t)) {
            heap_block_t* new_block = (heap_block_t*)((u8*)curr + sizeof(heap_block_t) + aligned);
            new_block->size = curr->size - aligned - sizeof(heap_block_t);
            new_block->next = curr->next;
            new_block->prev = curr;
            new_block->free = true;

            if (curr->next) {
                curr->next->prev = new_block;
            }

            curr->size = aligned;
            curr->next = new_block;
            curr->free = false;

            void* result = (void*)((u8*)curr + sizeof(heap_block_t));
            heap_irq_restore(irq_flags);
            return result;
        }
        curr = curr->next;
    }
    heap_irq_restore(irq_flags);
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    if (!heap_head) return;

    u64 irq_flags = heap_irq_save();
    heap_block_t* block = (heap_block_t*)((u8*)ptr - sizeof(heap_block_t));
    block->free = true;

    // Merge with next if free
    if (block->next && block->next->free) {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next) {
            block->next->prev = block;
        }
    }

    // Merge with previous using prev pointer - O(1) instead of O(n)
    heap_block_t* prev = block->prev;
    if (prev && prev->free) {
        prev->size += sizeof(heap_block_t) + block->size;
        prev->next = block->next;
        if (block->next) {
            block->next->prev = prev;
        }
    }
    heap_irq_restore(irq_flags);
}