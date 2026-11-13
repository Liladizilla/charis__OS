/* Transactional IPC descriptor transfer host regressions. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/ipc.h>
#include <kernel/security.h>
#include <kernel/vfs.h>

static task_t task_a, task_b;
static task_t* active_task;
static task_t* registry[16];
static u32 registry_count;
static u32 close_counts[512];

task_t* scheduler_current(void) { return active_task; }

task_t* task_find_by_pid(u32 pid) {
    for (u32 i = 0; i < registry_count; i++)
        if (registry[i] && registry[i]->pid == pid) return registry[i];
    return NULL;
}

void scheduler_yield(void) {}

void task_unblock(task_t* task) {
    if (!task || task->state != TASK_STATE_BLOCKED) return;
    task->state = (task == active_task) ? TASK_STATE_RUNNING : TASK_STATE_READY;
}

bool security_check_capability(task_t* task, u32 capability) {
    return task && capability &&
           (task->capabilities & capability) == capability;
}

void* kmalloc(usize size) { return malloc(size); }
void kfree(void* pointer) { free(pointer); }

static int node_read(vfs_node_t* node, u64 offset, u64 size, u8* buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static int node_write(vfs_node_t* node, u64 offset, u64 size,
                      const u8* buffer) {
    (void)node; (void)offset; (void)buffer;
    return (int)size;
}

static void node_close(vfs_node_t* node) {
    assert(node != NULL);
    assert(node->inode < sizeof(close_counts) / sizeof(close_counts[0]));
    close_counts[node->inode]++;
}

static void register_task(task_t* task, u32 pid) {
    *task = (task_t){0};
    task->pid = pid;
    task->state = TASK_STATE_READY;
    task->scheduler_queued = true;
    fd_table_init(task->fd_table);
    assert(registry_count < sizeof(registry) / sizeof(registry[0]));
    registry[registry_count++] = task;
}

static void reset_pair(void) {
    registry_count = 0;
    memset(close_counts, 0, sizeof(close_counts));
    register_task(&task_a, 701);
    register_task(&task_b, 702);
    ipc_init();
}

static vfs_node_t make_node(u64 id) {
    vfs_node_t node = {
        .flags = VFS_FILE,
        .inode = id,
        .read = node_read,
        .write = node_write,
        .close = node_close
    };
    return node;
}

static fd_handle_t open_node(task_t* owner, vfs_node_t* node, u32 rights) {
    active_task = owner;
    fd_alloc_status_t status = FD_ALLOC_FAILED;
    fd_handle_t handle = fd_alloc(node, 0, rights, &status);
    assert(handle != FD_HANDLE_INVALID && status == FD_ALLOC_OK);
    return handle;
}

static void close_endpoint(task_t* owner, ipc_handle_t endpoint) {
    active_task = owner;
    assert(ipc_close(endpoint) == IPC_STATUS_OK);
}

static void create_pair(ipc_handle_t* sender, ipc_handle_t* receiver) {
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, sender, receiver));
}

static void test_rights_are_attenuated_and_plain_receive_preserves_message(void) {
    reset_pair();
    vfs_node_t node = make_node(1);
    fd_handle_t source = open_node(&task_a, &node,
                                   FD_RIGHT_READ | FD_RIGHT_WRITE);
    ipc_handle_t send_endpoint, receive_endpoint;
    create_pair(&send_endpoint, &receive_endpoint);

    active_task = &task_a;
    assert(ipc_send_with_fd(send_endpoint, "cap", 4, source,
                            FD_RIGHT_READ) == IPC_STATUS_OK);

    vfs_open_file_t* pin = fd_acquire_from(&task_a, source, FD_RIGHT_READ);
    assert(pin != NULL);
    assert((pin->lifecycle & VFS_OPEN_FILE_REF_MASK) == 3u);
    fd_release(pin);

    assert(fd_close(source));
    assert(close_counts[1] == 0u); /* The queue now owns the remaining ref. */

    active_task = &task_b;
    char payload[8] = {0};
    u32 sender = 0;
    assert(ipc_recv(receive_endpoint, payload, sizeof(payload), &sender) ==
           IPC_STATUS_ATTACHMENT_REQUIRED);
    assert(sender == 0u); /* The failed receive did not consume metadata. */

    fd_handle_t received = FD_HANDLE_INVALID;
    assert(ipc_recv_with_fd(receive_endpoint, payload, sizeof(payload),
                            &sender, &received) == 4);
    assert(memcmp(payload, "cap", 4) == 0 && sender == task_a.pid);
    assert(received != FD_HANDLE_INVALID);
    assert(fd_has_rights(received, FD_RIGHT_READ));
    assert(!fd_has_rights(received, FD_RIGHT_WRITE));

    vfs_open_file_t* imported = fd_acquire_from(&task_b, received,
                                                 FD_RIGHT_READ);
    assert(imported != NULL);
    assert((imported->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2u);
    fd_release(imported);

    assert(fd_close(received));
    assert(close_counts[1] == 1u);
    close_endpoint(&task_a, send_endpoint);
    close_endpoint(&task_b, receive_endpoint);
}

static void test_insufficient_source_rights_and_full_queue_release_pin(void) {
    reset_pair();
    vfs_node_t node = make_node(2);
    fd_handle_t source = open_node(&task_a, &node, FD_RIGHT_READ);
    ipc_handle_t send_endpoint, receive_endpoint;
    create_pair(&send_endpoint, &receive_endpoint);

    active_task = &task_a;
    assert(ipc_send_with_fd(send_endpoint, NULL, 0, source,
                            FD_RIGHT_WRITE) == IPC_STATUS_ACCESS_DENIED);

    for (u32 i = 0; i < IPC_MAX_MESSAGES; i++)
        assert(ipc_send(send_endpoint, "x", 1) == IPC_STATUS_OK);

    vfs_open_file_t* pin = fd_acquire_from(&task_a, source, FD_RIGHT_READ);
    assert(pin != NULL);
    assert((pin->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2u);
    fd_release(pin);

    assert(ipc_send_with_fd(send_endpoint, "fd", 3, source,
                            FD_RIGHT_READ) == IPC_STATUS_FULL);

    pin = fd_acquire_from(&task_a, source, FD_RIGHT_READ);
    assert(pin != NULL);
    assert((pin->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2u);
    fd_release(pin);
    assert(fd_close(source));
    assert(close_counts[2] == 1u);

    /* Discard the normal messages and release their endpoint resources. */
    close_endpoint(&task_b, receive_endpoint);
    close_endpoint(&task_a, send_endpoint);
}

static void test_recipient_full_table_preserves_message_for_retry(void) {
    reset_pair();
    vfs_node_t source_node = make_node(3);
    vfs_node_t filler_nodes[MAX_FDS];
    fd_handle_t filler_handles[MAX_FDS];
    fd_handle_t source = open_node(&task_a, &source_node, FD_RIGHT_READ);
    ipc_handle_t send_endpoint, receive_endpoint;
    create_pair(&send_endpoint, &receive_endpoint);

    active_task = &task_a;
    assert(ipc_send_with_fd(send_endpoint, "retry", 6, source,
                            FD_RIGHT_READ) == IPC_STATUS_OK);
    assert(fd_close(source));
    assert(close_counts[3] == 0u);

    active_task = &task_b;
    for (u32 i = 3; i < MAX_FDS; i++) {
        filler_nodes[i] = make_node(20u + i);
        filler_handles[i] = open_node(&task_b, &filler_nodes[i], FD_RIGHT_READ);
    }

    char payload[16] = {0};
    u32 sender = 0;
    fd_handle_t imported = FD_HANDLE_INVALID;
    assert(ipc_recv_with_fd(receive_endpoint, payload, sizeof(payload),
                            &sender, &imported) == IPC_STATUS_DESCRIPTOR_FULL);
    assert(sender == 0u && imported == FD_HANDLE_INVALID);
    assert(close_counts[3] == 0u); /* Still owned by the unconsumed queue. */

    assert(fd_close(filler_handles[3]));
    memset(payload, 0, sizeof(payload));
    assert(ipc_recv_with_fd(receive_endpoint, payload, sizeof(payload),
                            &sender, &imported) == 6);
    assert(memcmp(payload, "retry", 6) == 0 && sender == task_a.pid);
    assert(imported != FD_HANDLE_INVALID);
    assert(fd_has_rights(imported, FD_RIGHT_READ));
    assert(fd_close(imported));
    assert(close_counts[3] == 1u);

    fd_table_close_all(task_b.fd_table);
    close_endpoint(&task_a, send_endpoint);
    close_endpoint(&task_b, receive_endpoint);
}

static void test_blocking_receive_imports_and_endpoint_teardown_releases_queue(void) {
    reset_pair();
    vfs_node_t first_node = make_node(4);
    fd_handle_t source = open_node(&task_a, &first_node, FD_RIGHT_READ);
    ipc_handle_t send_endpoint, receive_endpoint;
    create_pair(&send_endpoint, &receive_endpoint);

    active_task = &task_a;
    assert(ipc_send_with_fd(send_endpoint, "wait", 5, source,
                            FD_RIGHT_READ) == IPC_STATUS_OK);
    assert(fd_close(source));

    active_task = &task_b;
    char payload[8] = {0};
    u32 sender = 0;
    fd_handle_t received = FD_HANDLE_INVALID;
    assert(ipc_recv_wait(receive_endpoint, payload, sizeof(payload), &sender) ==
           IPC_STATUS_ATTACHMENT_REQUIRED);
    assert(ipc_recv_with_fd_wait(receive_endpoint, payload, sizeof(payload),
                                 &sender, &received) == 5);
    assert(memcmp(payload, "wait", 5) == 0 && sender == task_a.pid);
    assert(received != FD_HANDLE_INVALID);
    assert(fd_close(received));
    assert(close_counts[4] == 1u);

    vfs_node_t queued_node = make_node(5);
    fd_handle_t queued_source = open_node(&task_a, &queued_node, FD_RIGHT_READ);
    active_task = &task_a;
    assert(ipc_send_with_fd(send_endpoint, "drop", 5, queued_source,
                            FD_RIGHT_READ) == IPC_STATUS_OK);
    assert(fd_close(queued_source));
    assert(close_counts[5] == 0u);

    /* Closing one endpoint releases its inbound queued reference. */
    close_endpoint(&task_b, receive_endpoint);
    assert(close_counts[5] == 1u);
    close_endpoint(&task_a, send_endpoint);

    /* Task exit also detaches and releases attachments on every owned endpoint. */
    ipc_handle_t second_sender, second_receiver;
    create_pair(&second_sender, &second_receiver);
    vfs_node_t exit_node = make_node(6);
    fd_handle_t exit_source = open_node(&task_a, &exit_node, FD_RIGHT_READ);
    active_task = &task_a;
    assert(ipc_send_with_fd(second_sender, "exit", 5, exit_source,
                            FD_RIGHT_READ) == IPC_STATUS_OK);
    assert(fd_close(exit_source));
    assert(close_counts[6] == 0u);
    ipc_task_close_all(&task_b);
    assert(close_counts[6] == 1u);
    ipc_task_close_all(&task_a);
}

int main(void) {
    test_rights_are_attenuated_and_plain_receive_preserves_message();
    test_insufficient_source_rights_and_full_queue_release_pin();
    test_recipient_full_table_preserves_message_for_retry();
    test_blocking_receive_imports_and_endpoint_teardown_releases_queue();
    puts("Transactional IPC descriptor transfer tests passed");
    return 0;
}
