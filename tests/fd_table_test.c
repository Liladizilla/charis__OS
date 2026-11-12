#include <assert.h>
#include <stdio.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

static task_t* active_task;
static int close_count;
static int observed_revoked_handle;
static int observed_fd = -1;

vfs_node_t dev_kbd = { .flags = VFS_CHARDEV };
vfs_node_t dev_vga = { .flags = VFS_CHARDEV };

task_t* scheduler_current(void) {
    return active_task;
}

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
    if (observed_fd >= 0 && !fd_get(observed_fd)) observed_revoked_handle++;
}

static vfs_node_t read_write_node = {
    .flags = VFS_FILE,
    .read = test_read,
    .write = test_write,
    .close = test_close
};

static vfs_node_t write_only_node = {
    .flags = VFS_FILE,
    .write = test_write,
    .close = test_close
};

static void reset_task(task_t* task) {
    *task = (task_t){0};
}

static void test_rights_are_narrow_and_validated(void) {
    task_t task;
    reset_task(&task);
    active_task = &task;

    assert(fd_alloc(NULL, 0, FD_RIGHT_READ) == -1);
    assert(fd_alloc(&read_write_node, 0, 0) == -1);
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_MASK | 4U) == -1);
    assert(fd_alloc(&write_only_node, 0, FD_RIGHT_READ) == -1);

    int read_fd = fd_alloc(&read_write_node, 7, FD_RIGHT_READ);
    assert(read_fd == 3);
    assert(fd_get(read_fd) != NULL);
    assert(fd_get(read_fd)->flags == 7);
    assert(fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(!fd_has_rights(read_fd, FD_RIGHT_WRITE));
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ | FD_RIGHT_WRITE));
    assert(!fd_has_rights(read_fd, 0));
    assert(!fd_has_rights(read_fd, FD_RIGHT_MASK | 8U));
    assert(fd_get(-1) == NULL);
    assert(fd_get(MAX_FDS) == NULL);

    int write_fd = fd_alloc(&write_only_node, 0, FD_RIGHT_WRITE);
    assert(write_fd == 4);
    assert(fd_has_rights(write_fd, FD_RIGHT_WRITE));
    assert(!fd_has_rights(write_fd, FD_RIGHT_READ));

    observed_fd = read_fd;
    assert(fd_close(read_fd));
    assert(observed_revoked_handle == 1);
    assert(fd_get(read_fd) == NULL);
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(!fd_close(read_fd));
    assert(!fd_close(FD_STDIN));
    observed_fd = -1;

    fd_table_close_all(task.fd_table);
    assert(fd_get(3) == NULL);
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

static void test_descriptor_tables_are_task_local(void) {
    task_t first, second;
    reset_task(&first);
    reset_task(&second);

    active_task = &first;
    int first_fd = fd_alloc(&read_write_node, 0, FD_RIGHT_READ);
    assert(first_fd == 3);

    active_task = &second;
    assert(fd_get(3) == NULL);
    assert(!fd_has_rights(3, FD_RIGHT_READ));
    int second_fd = fd_alloc(&read_write_node, 0, FD_RIGHT_WRITE);
    assert(second_fd == 3);
    assert(fd_has_rights(3, FD_RIGHT_WRITE));
    assert(!fd_has_rights(3, FD_RIGHT_READ));

    active_task = &first;
    assert(fd_has_rights(3, FD_RIGHT_READ));
    assert(!fd_has_rights(3, FD_RIGHT_WRITE));
    assert(fd_close(first_fd));

    active_task = &second;
    assert(fd_close(second_fd));
    fd_table_close_all(first.fd_table);
    fd_table_close_all(second.fd_table);
}

static void test_descriptor_capacity_and_cleanup(void) {
    task_t task;
    reset_task(&task);
    active_task = &task;
    close_count = 0;

    for (int i = 3; i < MAX_FDS; i++) {
        assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ) == i);
    }
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ) == -1);
    fd_table_close_all(task.fd_table);
    assert(close_count == MAX_FDS - 3);
    for (int i = 3; i < MAX_FDS; i++) {
        assert(fd_get(i) == NULL);
        assert(task.fd_table[i].rights == 0);
    }
}

int main(void) {
    test_rights_are_narrow_and_validated();
    test_descriptor_tables_are_task_local();
    test_descriptor_capacity_and_cleanup();
    puts("PASS: VFS descriptor rights, isolation and lifecycle tests");
    return 0;
}
