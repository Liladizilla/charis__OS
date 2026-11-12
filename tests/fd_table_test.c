#include <assert.h>
#include <stdio.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

static task_t* active_task;
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

static void test_rights_and_stale_handle_rejection(void) {
    task_t task;
    reset_task(&task, 41);
    active_task = &task;

    assert(fd_alloc(NULL, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    assert(fd_alloc(&read_write_node, 0, 0) == FD_HANDLE_INVALID);
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_MASK | 4U) == FD_HANDLE_INVALID);
    assert(fd_alloc(&write_only_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);

    fd_handle_t read_fd = fd_alloc(&read_write_node, 7, FD_RIGHT_READ);
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

    fd_handle_t write_fd = fd_alloc(&write_only_node, 0, FD_RIGHT_WRITE);
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

    fd_handle_t reopened_fd = fd_alloc(&read_write_node, 0, FD_RIGHT_READ);
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
    fd_handle_t first_handle = fd_alloc(&read_write_node, 0, FD_RIGHT_READ);
    assert(first_handle != FD_HANDLE_INVALID && fd_get(first_handle));

    active_task = &second;
    assert(fd_get(first_handle) == NULL);
    assert(!fd_has_rights(first_handle, FD_RIGHT_READ));
    fd_handle_t second_handle = fd_alloc(&write_only_node, 0, FD_RIGHT_WRITE);
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
    task_t task;
    reset_task(&task, 59);
    active_task = &task;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    fd_handle_t first = fd_alloc(&read_write_node, 0, FD_RIGHT_READ);
    assert(first != FD_HANDLE_INVALID);
    assert(fd_get(first) != NULL);

    /* Reattaching a closeable node would double-run its release callback. */
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    assert(fd_get(first) != NULL);
    assert(fd_close(first));
    assert(close_count == 1);
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
        fd_handle_t handle = fd_alloc(&capacity_nodes[i], 0, FD_RIGHT_READ);
        assert(handle != FD_HANDLE_INVALID);
        assert(FD_HANDLE_SLOT(handle) == i);
    }
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
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

    fd_handle_t handle = fd_alloc(&read_write_node, 0, FD_RIGHT_READ);
    assert(handle != FD_HANDLE_INVALID);
    assert(FD_HANDLE_SLOT(handle) == 4);
    assert(fd_close(handle));

    for (u32 i = 3; i < MAX_FDS; i++)
        task.fd_table[i].generation = FD_HANDLE_GENERATION_MAX;
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    fd_table_close_all(task.fd_table);
}

int main(void) {
    test_rights_and_stale_handle_rejection();
    test_handles_are_bound_to_the_owner_task();
    test_closeable_node_has_one_descriptor_owner();
    test_capacity_and_cleanup();
    test_generation_exhaustion_fails_closed();
    puts("PASS: task-bound descriptor handles, stale-reference rejection, rights and lifecycle");
    return 0;
}
