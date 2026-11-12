#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <kernel/task_id.h>

#define THREAD_COUNT 8U
#define ALLOCATIONS_PER_THREAD 4096U
#define TOTAL_ALLOCATIONS (THREAD_COUNT * ALLOCATIONS_PER_THREAD)

static u64 shared_next_pid = 1;
static u32 allocated[THREAD_COUNT][ALLOCATIONS_PER_THREAD];

static void* allocate_many(void* context) {
    u32 thread_index = (u32)(uintptr_t)context;
    for (u32 i = 0; i < ALLOCATIONS_PER_THREAD; i++) {
        u32 pid = task_pid_allocate(&shared_next_pid);
        if (!pid) return (void*)1;
        allocated[thread_index][i] = pid;
    }
    return NULL;
}

static int compare_pid(const void* left, const void* right) {
    u32 a = *(const u32*)left;
    u32 b = *(const u32*)right;
    return (a > b) - (a < b);
}

int main(void) {
    pthread_t threads[THREAD_COUNT];
    u32 sorted[TOTAL_ALLOCATIONS];

    for (u32 i = 0; i < THREAD_COUNT; i++)
        assert(pthread_create(&threads[i], NULL, allocate_many,
                              (void*)(uintptr_t)i) == 0);

    for (u32 i = 0; i < THREAD_COUNT; i++) {
        void* result = NULL;
        assert(pthread_join(threads[i], &result) == 0);
        assert(result == NULL);
    }

    u32 cursor = 0;
    for (u32 thread = 0; thread < THREAD_COUNT; thread++) {
        for (u32 i = 0; i < ALLOCATIONS_PER_THREAD; i++)
            sorted[cursor++] = allocated[thread][i];
    }

    qsort(sorted, TOTAL_ALLOCATIONS, sizeof(sorted[0]), compare_pid);
    for (u32 i = 0; i < TOTAL_ALLOCATIONS; i++)
        assert(sorted[i] == i + 1);

    assert(__atomic_load_n(&shared_next_pid, __ATOMIC_RELAXED) ==
           (u64)TOTAL_ALLOCATIONS + 1);
    puts("PASS: concurrent PID allocation is unique and monotonic");
    return 0;
}
