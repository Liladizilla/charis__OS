/* Concurrent host regression for the single-CPU kernel heap critical section. */
#define HEAP_HOST_TEST 1
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <kernel/types.h>

#define HEAP_TEST_THREADS 8
#define HEAP_TEST_SLOTS 16
#define HEAP_TEST_ITERATIONS 1200
#define HEAP_TEST_BYTES (2u * 1024u * 1024u)

static pthread_mutex_t heap_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint save_calls;
static atomic_uint restore_calls;

u64 heap_test_irq_save(void) {
    int result = pthread_mutex_lock(&heap_mutex);
    assert(result == 0);
    atomic_fetch_add_explicit(&save_calls, 1u, memory_order_relaxed);
    return 1u; /* The host critical section was entered. */
}
void heap_test_irq_restore(u64 flags) {
    assert(flags == 1u);
    atomic_fetch_add_explicit(&restore_calls, 1u, memory_order_relaxed);
    int result = pthread_mutex_unlock(&heap_mutex);
    assert(result == 0);
}

#include "../kernel/heap.c"

typedef struct {
    unsigned id;
    pthread_barrier_t* start;
} worker_args_t;

static union {
    max_align_t align;
    u8 bytes[HEAP_TEST_BYTES];
} heap_storage;

static void* heap_worker(void* opaque) {
    worker_args_t* args = (worker_args_t*)opaque;
    pthread_barrier_wait(args->start);

    for (unsigned iteration = 0; iteration < HEAP_TEST_ITERATIONS; iteration++) {
        u8* blocks[HEAP_TEST_SLOTS] = {0};
        usize lengths[HEAP_TEST_SLOTS];

        for (unsigned slot = 0; slot < HEAP_TEST_SLOTS; slot++) {
            usize length = 24u +
                (usize)((args->id * 131u + iteration * 17u + slot * 29u) % 1000u);
            lengths[slot] = length;
            blocks[slot] = (u8*)kmalloc(length);
            assert(blocks[slot] != NULL);
            memset(blocks[slot], (int)(args->id + slot + iteration), length);
            if ((slot & 3u) == 0u) sched_yield();
        }

        for (unsigned slot = 0; slot < HEAP_TEST_SLOTS; slot++) {
            u8 expected = (u8)(args->id + slot + iteration);
            for (usize i = 0; i < lengths[slot]; i++)
                assert(blocks[slot][i] == expected);
        }

        for (unsigned slot = HEAP_TEST_SLOTS; slot > 0; slot--) {
            kfree(blocks[slot - 1u]);
            if ((slot & 3u) == 0u) sched_yield();
        }
    }
    return NULL;
}

int main(void) {
    heap_init(heap_storage.bytes, sizeof(heap_storage.bytes));
    assert(kmalloc(0u) == NULL);
    assert(kmalloc((usize)-1) == NULL);

    pthread_t threads[HEAP_TEST_THREADS];
    worker_args_t args[HEAP_TEST_THREADS];
    pthread_barrier_t start;
    assert(pthread_barrier_init(&start, NULL, HEAP_TEST_THREADS) == 0);

    for (unsigned i = 0; i < HEAP_TEST_THREADS; i++) {
        args[i].id = i + 1u;
        args[i].start = &start;
        assert(pthread_create(&threads[i], NULL, heap_worker, &args[i]) == 0);
    }
    for (unsigned i = 0; i < HEAP_TEST_THREADS; i++)
        assert(pthread_join(threads[i], NULL) == 0);

    assert(pthread_barrier_destroy(&start) == 0);
    assert(atomic_load_explicit(&save_calls, memory_order_relaxed) ==
           atomic_load_explicit(&restore_calls, memory_order_relaxed));
    assert(atomic_load_explicit(&save_calls, memory_order_relaxed) >
           HEAP_TEST_THREADS * HEAP_TEST_ITERATIONS);

    /* All worker allocations were returned; adjacent blocks must coalesce. */
    void* large = kmalloc(HEAP_TEST_BYTES / 2u);
    assert(large != NULL);
    memset(large, 0xA5, HEAP_TEST_BYTES / 2u);
    kfree(large);

    puts("heap preemption critical-section regression: PASS");
    return 0;
}
