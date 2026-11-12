#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

static __thread task_t* active_task;
static int close_count;
static int observed_revoked_handle;
static fd_handle_t observed_fd = FD_HANDLE_INVALID;
static vfs_node_t capacity_nodes[MAX_FDS];

extern vfs_node_t dev_kbd;
extern vfs_node_t dev_vga;

task_t* scheduler_current(void) { return active_task; }

static int test_read(vfs_node_t* node, u64 offset, u64 size, u8* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}
static int test_write(vfs_node_t* node, u64 offset, u64 size, const u8* buffer) {
    (void)node; (void)offset; (void)buffer;
    return (int)size;
}
static void test_close(vfs_node_t* node) {
    (void)node;
    close_count++;
    if (observed_fd != FD_HANDLE_INVALID && !fd_get(observed_fd))
        observed_revoked_handle++;
}
static vfs_node_t read_write_node = {
    .flags = VFS_FILE, .read = test_read, .write = test_write, .close = test_close
};
static vfs_node_t write_only_node = {
    .flags = VFS_FILE, .write = test_write, .close = test_close
};

static void reset_task(task_t* task, u32 pid) {
    *task = (task_t){0};
    task->pid = pid;
}

static fd_handle_t alloc_fd(vfs_node_t* node, u32 flags, u32 rights) {
    fd_alloc_status_t status;
    return fd_alloc(node, flags, rights, &status);
}

static void test_rights_and_stale_handle_rejection(void) {
    task_t task;
    reset_task(&task, 41);
    active_task = &task;

    assert(alloc_fd(NULL, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    assert(alloc_fd(&read_write_node, 0, 0) == FD_HANDLE_INVALID);
    assert(alloc_fd(&read_write_node, 0, FD_RIGHT_MASK | 4U) == FD_HANDLE_INVALID);
    assert(alloc_fd(&write_only_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);

    fd_handle_t read_fd = alloc_fd(&read_write_node, 7, FD_RIGHT_READ);
    assert(read_fd == (((u64)41 << 32) | (1ULL << FD_HANDLE_SLOT_BITS) | 3ULL));
    assert(fd_get(read_fd) != NULL);
    assert(fd_get(3) == NULL);
    assert(fd_get((fd_handle_t)-1) == NULL);
    assert(fd_get(FD_HANDLE_INVALID) == NULL);
    assert(fd_get(MAX_FDS) == NULL);
    assert(fd_get(read_fd)->flags == 7);
    assert(fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(!fd_has_rights(read_fd, FD_RIGHT_WRITE));
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ | FD_RIGHT_WRITE));
    assert(!fd_has_rights(read_fd, 0));
    assert(!fd_has_rights(read_fd, FD_RIGHT_MASK | 8U));

    fd_handle_t write_fd = alloc_fd(&write_only_node, 0, FD_RIGHT_WRITE);
    assert(FD_HANDLE_SLOT(write_fd) == 4);
    assert(fd_has_rights(write_fd, FD_RIGHT_WRITE));
    assert(!fd_has_rights(write_fd, FD_RIGHT_READ));

    observed_fd = read_fd;
    assert(fd_close(read_fd));
    assert(observed_revoked_handle == 1);
    assert(fd_get(read_fd) == NULL);
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(!fd_close(read_fd));
    assert(!fd_close(FD_STDIN));

    fd_handle_t reopened_fd = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(FD_HANDLE_SLOT(reopened_fd) == 3);
    assert(reopened_fd != read_fd);
    assert(fd_get(read_fd) == NULL);
    assert(fd_get(reopened_fd) != NULL);
    observed_fd = FD_HANDLE_INVALID;

    fd_table_close_all(task.fd_table);
    assert(fd_get(reopened_fd) == NULL);
    assert(task.fd_table[3].generation == 2);
    assert(task.fd_table[FD_STDIN].node == &dev_kbd);
    assert(task.fd_table[FD_STDIN].rights == FD_RIGHT_READ);
    assert(task.fd_table[FD_STDOUT].node == &dev_vga);
    assert(task.fd_table[FD_STDOUT].rights == FD_RIGHT_WRITE);
    assert(task.fd_table[FD_STDERR].node == &dev_vga);
    assert(task.fd_table[FD_STDERR].rights == FD_RIGHT_WRITE);
    assert(fd_has_rights(FD_STDIN, FD_RIGHT_READ));
    assert(!fd_has_rights(FD_STDIN, FD_RIGHT_WRITE));
    assert(fd_has_rights(FD_STDOUT, FD_RIGHT_WRITE));
}

static void test_handles_are_bound_to_the_owner_task(void) {
    task_t first, second;
    reset_task(&first, 51);
    reset_task(&second, 52);

    active_task = &first;
    fd_handle_t first_handle = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(first_handle != FD_HANDLE_INVALID && fd_get(first_handle));

    active_task = &second;
    assert(fd_get(first_handle) == NULL);
    assert(!fd_has_rights(first_handle, FD_RIGHT_READ));
    fd_handle_t second_handle = alloc_fd(&write_only_node, 0, FD_RIGHT_WRITE);
    assert(second_handle != FD_HANDLE_INVALID);
    assert(second_handle != first_handle);
    assert(FD_HANDLE_SLOT(second_handle) == FD_HANDLE_SLOT(first_handle));
    assert(fd_has_rights(second_handle, FD_RIGHT_WRITE));
    assert(!fd_has_rights(second_handle, FD_RIGHT_READ));
    assert(fd_close(second_handle));

    active_task = &first;
    assert(fd_has_rights(first_handle, FD_RIGHT_READ));
    assert(!fd_has_rights(first_handle, FD_RIGHT_WRITE));
    assert(fd_close(first_handle));
    fd_table_close_all(first.fd_table);
    fd_table_close_all(second.fd_table);
}

static void test_closeable_node_has_one_descriptor_owner(void) {
    task_t first_task, second_task;
    reset_task(&first_task, 59);
    reset_task(&second_task, 60);
    active_task = &first_task;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    fd_handle_t first = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(first != FD_HANDLE_INVALID);
    assert(fd_get(first) != NULL);

    active_task = &second_task;
    fd_alloc_status_t status = FD_ALLOC_FAILED;
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ, &status) ==
           FD_HANDLE_INVALID);
    assert(status == FD_ALLOC_NODE_ALREADY_OWNED);
    assert(close_count == 0);

    active_task = &first_task;
    assert(fd_get(first) != NULL);
    assert(fd_close(first));
    assert(close_count == 1);

    active_task = &second_task;
    fd_handle_t second = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(second != FD_HANDLE_INVALID);
    assert(fd_close(second));
    assert(close_count == 2);
}

static void test_capacity_and_cleanup(void) {
    task_t task;
    reset_task(&task, 61);
    active_task = &task;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    for (u32 i = 3; i < MAX_FDS; i++) {
        capacity_nodes[i] = (vfs_node_t){
            .flags = VFS_FILE,
            .read = test_read,
            .write = test_write,
            .close = test_close
        };
        fd_handle_t handle = alloc_fd(&capacity_nodes[i], 0, FD_RIGHT_READ);
        assert(handle != FD_HANDLE_INVALID);
        assert(FD_HANDLE_SLOT(handle) == i);
    }
    assert(alloc_fd(&read_write_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    fd_table_close_all(task.fd_table);
    assert(close_count == MAX_FDS - 3);
    for (u32 i = 3; i < MAX_FDS; i++) {
        fd_handle_t stale = ((u64)61 << 32) |
            ((u64)task.fd_table[i].generation << FD_HANDLE_SLOT_BITS) | i;
        assert(fd_get(stale) == NULL);
        assert(task.fd_table[i].rights == 0);
        assert(task.fd_table[i].generation == 1);
    }
}

static void test_generation_exhaustion_fails_closed(void) {
    task_t task;
    reset_task(&task, 71);
    active_task = &task;
    task.fd_table[3].generation = FD_HANDLE_GENERATION_MAX;

    fd_handle_t handle = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(handle != FD_HANDLE_INVALID);
    assert(FD_HANDLE_SLOT(handle) == 4);
    assert(fd_close(handle));

    for (u32 i = 3; i < MAX_FDS; i++)
        task.fd_table[i].generation = FD_HANDLE_GENERATION_MAX;
    assert(alloc_fd(&read_write_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    fd_table_close_all(task.fd_table);
}

static void test_maximum_identity_descriptor_is_valid(void) {
    task_t task;
    reset_task(&task, 0xFFFFFFFFU);
    active_task = &task;

    /* The final slot/generation/PID tuple is UINT64_MAX. It is valid because
     * the invalid sentinel uses owner PID zero, which the allocator never issues. */
    for (u32 i = 3; i < MAX_FDS - 1; i++)
        task.fd_table[i].generation = FD_HANDLE_GENERATION_MAX;
    task.fd_table[MAX_FDS - 1].generation = FD_HANDLE_GENERATION_MAX - 1;

    fd_handle_t handle = ((u64)task.pid << 32) |
        ((u64)FD_HANDLE_GENERATION_MAX << FD_HANDLE_SLOT_BITS) |
        (u64)FD_HANDLE_SLOT_MASK;
    assert(handle == ~(fd_handle_t)0);
    assert(handle != FD_HANDLE_INVALID);
    assert(fd_get(FD_HANDLE_INVALID) == NULL);

    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;
    fd_alloc_status_t status = FD_ALLOC_FAILED;
    fd_handle_t allocated = fd_alloc(&read_write_node, 0, FD_RIGHT_READ, &status);
    assert(allocated == handle);
    assert(status == FD_ALLOC_OK);
    assert(fd_get(allocated) != NULL);
    assert(fd_has_rights(allocated, FD_RIGHT_READ));
    assert(fd_close(allocated));
    assert(close_count == 1);
}

typedef struct {
    task_t* task;
    fd_handle_t handle;
    fd_alloc_status_t status;
} concurrent_owner_args_t;

static pthread_barrier_t owner_start_barrier;

static void* concurrent_owner_alloc(void* opaque) {
    concurrent_owner_args_t* args = (concurrent_owner_args_t*)opaque;
    active_task = args->task;
    (void)pthread_barrier_wait(&owner_start_barrier);
    args->handle = fd_alloc(&read_write_node, 0, FD_RIGHT_READ, &args->status);
    return NULL;
}

static void test_concurrent_closeable_node_claim_is_unique(void) {
    task_t first_task, second_task;
    reset_task(&first_task, 81);
    reset_task(&second_task, 82);
    close_count = 0;

    concurrent_owner_args_t first = {
        .task = &first_task, .handle = FD_HANDLE_INVALID,
        .status = FD_ALLOC_FAILED
    };
    concurrent_owner_args_t second = {
        .task = &second_task, .handle = FD_HANDLE_INVALID,
        .status = FD_ALLOC_FAILED
    };
    pthread_t first_thread, second_thread;
    assert(pthread_barrier_init(&owner_start_barrier, NULL, 3) == 0);
    assert(pthread_create(&first_thread, NULL, concurrent_owner_alloc, &first) == 0);
    assert(pthread_create(&second_thread, NULL, concurrent_owner_alloc, &second) == 0);
    (void)pthread_barrier_wait(&owner_start_barrier);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(pthread_barrier_destroy(&owner_start_barrier) == 0);

    bool first_won = first.handle != FD_HANDLE_INVALID;
    bool second_won = second.handle != FD_HANDLE_INVALID;
    assert(first_won != second_won);
    assert((first_won ? first.status : second.status) == FD_ALLOC_OK);
    assert((first_won ? second.status : first.status) ==
           FD_ALLOC_NODE_ALREADY_OWNED);

    active_task = first_won ? &first_task : &second_task;
    assert(fd_close(first_won ? first.handle : second.handle));
    assert(close_count == 1);
}

int main(void) {
    test_rights_and_stale_handle_rejection();
    test_handles_are_bound_to_the_owner_task();
    test_closeable_node_has_one_descriptor_owner();
    test_concurrent_closeable_node_claim_is_unique();
    test_capacity_and_cleanup();
    test_generation_exhaustion_fails_closed();
    test_maximum_identity_descriptor_is_valid();
    puts("PASS: task-bound descriptor handles, stale-reference rejection, rights and lifecycle");
    return 0;
}
