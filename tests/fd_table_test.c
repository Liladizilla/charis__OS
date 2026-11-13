#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <kernel/task.h>
#include <kernel/vfs.h>

static __thread task_t* active_task;
static int close_count;
static int observed_revoked_handle;
static bool fail_next_kmalloc;
static pthread_barrier_t owner_start_barrier;
static fd_handle_t observed_fd = FD_HANDLE_INVALID;
static vfs_node_t capacity_nodes[MAX_FDS];
static task_t* registered_tasks[128];
static u32 registered_pids[128];
static u32 registered_task_count;

extern vfs_node_t dev_kbd;
extern vfs_node_t dev_vga;

task_t* scheduler_current(void) { return active_task; }

task_t* task_find_by_pid(u32 pid) {
    for (u32 i = 0; i < registered_task_count; i++) {
        if (registered_pids[i] == pid) return registered_tasks[i];
    }
    return NULL;
}

void* kmalloc(usize size) {
    if (fail_next_kmalloc) {
        fail_next_kmalloc = false;
        return NULL;
    }
    return malloc(size);
}

void kfree(void* pointer) {
    free(pointer);
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
    __atomic_fetch_add(&close_count, 1, __ATOMIC_RELAXED);
    if (observed_fd != FD_HANDLE_INVALID &&
        !fd_has_rights(observed_fd, FD_RIGHT_READ))
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
    task->state = TASK_STATE_READY;
    fd_table_init(task->fd_table);
    assert(registered_task_count < 128);
    registered_tasks[registered_task_count] = task;
    registered_pids[registered_task_count] = pid;
    registered_task_count++;
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
    vfs_open_file_t* read_file = fd_acquire(read_fd, FD_RIGHT_READ);
    assert(read_file != NULL);
    assert(read_file->node == &read_write_node);
    assert(read_file->open_flags == 7);
    fd_release(read_file);
    assert(!fd_has_rights(3, FD_RIGHT_READ));
    assert(!fd_has_rights((fd_handle_t)-1, FD_RIGHT_READ));
    assert(!fd_has_rights(FD_HANDLE_INVALID, FD_RIGHT_READ));
    assert(!fd_has_rights(MAX_FDS, FD_RIGHT_READ));
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
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(!fd_close(read_fd));
    assert(!fd_close(FD_STDIN));

    fd_handle_t reopened_fd = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(FD_HANDLE_SLOT(reopened_fd) == 3);
    assert(reopened_fd != read_fd);
    assert(!fd_has_rights(read_fd, FD_RIGHT_READ));
    assert(fd_has_rights(reopened_fd, FD_RIGHT_READ));
    observed_fd = FD_HANDLE_INVALID;

    fd_table_close_all(task.fd_table);
    assert(!fd_has_rights(reopened_fd, FD_RIGHT_READ));
    assert(task.fd_table[3].generation == 2);
    assert(task.fd_table[FD_STDIN].file->node == &dev_kbd);
    assert(task.fd_table[FD_STDIN].rights == FD_RIGHT_READ);
    assert(task.fd_table[FD_STDOUT].file->node == &dev_vga);
    assert(task.fd_table[FD_STDOUT].rights == FD_RIGHT_WRITE);
    assert(task.fd_table[FD_STDERR].file->node == &dev_vga);
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
    assert(first_handle != FD_HANDLE_INVALID && fd_has_rights(first_handle, FD_RIGHT_READ));

    active_task = &second;
    assert(!fd_has_rights(first_handle, FD_RIGHT_READ));
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
    assert(fd_has_rights(first, FD_RIGHT_READ));

    active_task = &second_task;
    fd_alloc_status_t status = FD_ALLOC_FAILED;
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ, &status) ==
           FD_HANDLE_INVALID);
    assert(status == FD_ALLOC_NODE_ALREADY_OWNED);
    assert(close_count == 0);

    active_task = &first_task;
    assert(fd_has_rights(first, FD_RIGHT_READ));
    assert(fd_close(first));
    assert(close_count == 1);

    active_task = &second_task;
    fd_handle_t second = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(second != FD_HANDLE_INVALID);
    assert(fd_close(second));
    assert(close_count == 2);
}

static void test_descriptor_duplication_shares_file_and_attenuates_rights(void) {
    task_t sender, recipient;
    reset_task(&sender, 91);
    reset_task(&recipient, 92);
    active_task = &sender;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    fd_handle_t source = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(source != FD_HANDLE_INVALID);

    /* A recipient may never receive rights the source descriptor does not have. */
    assert(fd_duplicate_to(source, recipient.pid, FD_RIGHT_WRITE) ==
           FD_HANDLE_INVALID);
    vfs_open_file_t* source_before = fd_acquire(source, FD_RIGHT_READ);
    assert(source_before != NULL);
    assert((source_before->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2);
    fd_release(source_before);

    fd_handle_t duplicate = fd_duplicate_to(source, recipient.pid,
                                            FD_RIGHT_READ);
    assert(duplicate != FD_HANDLE_INVALID);
    assert((u32)(duplicate >> 32) == recipient.pid);
    assert(FD_HANDLE_SLOT(duplicate) == 3);

    vfs_open_file_t* sender_file = fd_acquire(source, FD_RIGHT_READ);
    active_task = &recipient;
    assert(fd_has_rights(duplicate, FD_RIGHT_READ));
    assert(!fd_has_rights(duplicate, FD_RIGHT_WRITE));
    vfs_open_file_t* recipient_file = fd_acquire(duplicate, FD_RIGHT_READ);
    assert(sender_file != NULL && recipient_file == sender_file);

    /* The shared open-file description, not a copied offset, is transferred. */
    sender_file->offset = 37;
    assert(recipient_file->offset == 37);
    fd_release(recipient_file);
    fd_release(sender_file);

    /* Sender close cannot destroy a file that still has a recipient entry. */
    active_task = &sender;
    assert(fd_close(source));
    assert(close_count == 0);
    active_task = &recipient;
    assert(fd_has_rights(duplicate, FD_RIGHT_READ));
    assert(fd_close(duplicate));
    assert(close_count == 1);
}

typedef struct {
    task_t* sender;
    task_t* recipient;
    fd_handle_t source;
    fd_handle_t duplicate;
    bool close_result;
} duplicate_close_race_t;

static pthread_barrier_t duplicate_close_barrier;

static void* duplicate_close_race_duplicate(void* opaque) {
    duplicate_close_race_t* args = (duplicate_close_race_t*)opaque;
    active_task = args->sender;
    (void)pthread_barrier_wait(&duplicate_close_barrier);
    args->duplicate = fd_duplicate_to(args->source, args->recipient->pid,
                                      FD_RIGHT_READ);
    return NULL;
}

static void* duplicate_close_race_close(void* opaque) {
    duplicate_close_race_t* args = (duplicate_close_race_t*)opaque;
    active_task = args->sender;
    (void)pthread_barrier_wait(&duplicate_close_barrier);
    args->close_result = fd_close(args->source);
    return NULL;
}

static void test_sender_close_races_descriptor_duplication(void) {
    task_t sender, recipient;
    reset_task(&sender, 97);
    reset_task(&recipient, 98);
    active_task = &sender;
    close_count = 0;

    fd_handle_t source = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(source != FD_HANDLE_INVALID);

    duplicate_close_race_t args = {
        .sender = &sender,
        .recipient = &recipient,
        .source = source,
        .duplicate = FD_HANDLE_INVALID,
        .close_result = false
    };
    pthread_t duplicate_thread, close_thread;
    assert(pthread_barrier_init(&duplicate_close_barrier, NULL, 3) == 0);
    assert(pthread_create(&duplicate_thread, NULL,
                          duplicate_close_race_duplicate, &args) == 0);
    assert(pthread_create(&close_thread, NULL,
                          duplicate_close_race_close, &args) == 0);
    (void)pthread_barrier_wait(&duplicate_close_barrier);
    assert(pthread_join(duplicate_thread, NULL) == 0);
    assert(pthread_join(close_thread, NULL) == 0);
    assert(pthread_barrier_destroy(&duplicate_close_barrier) == 0);

    assert(args.close_result);
    assert(!fd_has_rights(source, FD_RIGHT_READ));

    if (args.duplicate != FD_HANDLE_INVALID) {
        /* Duplication linearized first: the recipient owns the surviving ref. */
        active_task = &recipient;
        assert(fd_has_rights(args.duplicate, FD_RIGHT_READ));
        assert(fd_close(args.duplicate));
    } else {
        /* Close linearized first: duplication never resurrected the object. */
        active_task = &recipient;
        assert(!fd_has_rights(args.duplicate, FD_RIGHT_READ));
    }

    assert(close_count == 1);
}

static void test_recipient_exit_releases_only_its_duplicate_reference(void) {
    task_t sender, recipient;
    reset_task(&sender, 93);
    reset_task(&recipient, 94);
    active_task = &sender;
    close_count = 0;

    fd_handle_t source = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(source != FD_HANDLE_INVALID);
    fd_handle_t duplicate = fd_duplicate_to(source, recipient.pid,
                                            FD_RIGHT_READ);
    assert(duplicate != FD_HANDLE_INVALID);
    vfs_open_file_t* pin = fd_acquire(source, FD_RIGHT_READ);
    assert(pin != NULL);
    assert((pin->lifecycle & VFS_OPEN_FILE_REF_MASK) == 3);
    fd_release(pin);

    /* This is the descriptor-table portion of task exit. */
    fd_table_close_all(recipient.fd_table);
    active_task = &recipient;
    assert(!fd_has_rights(duplicate, FD_RIGHT_READ));
    assert(close_count == 0);

    active_task = &sender;
    assert(fd_has_rights(source, FD_RIGHT_READ));
    assert(fd_close(source));
    assert(close_count == 1);
}

static void test_duplicate_rejects_full_and_exiting_recipients(void) {
    task_t sender, recipient;
    vfs_node_t fillers[MAX_FDS];
    reset_task(&sender, 95);
    reset_task(&recipient, 96);
    active_task = &sender;
    close_count = 0;

    fd_handle_t source = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(source != FD_HANDLE_INVALID);

    active_task = &recipient;
    for (u32 i = 3; i < MAX_FDS; i++) {
        fillers[i] = (vfs_node_t){
            .flags = VFS_FILE, .read = test_read, .close = test_close
        };
        fd_alloc_status_t status = FD_ALLOC_FAILED;
        assert(fd_alloc(&fillers[i], 0, FD_RIGHT_READ, &status) !=
               FD_HANDLE_INVALID);
        assert(status == FD_ALLOC_OK);
    }

    active_task = &sender;
    vfs_open_file_t* pinned = fd_acquire(source, FD_RIGHT_READ);
    assert(pinned != NULL);
    assert((pinned->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2);
    assert(fd_duplicate_to(source, recipient.pid, FD_RIGHT_READ) ==
           FD_HANDLE_INVALID);
    assert((pinned->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2);
    assert(fd_has_rights(source, FD_RIGHT_READ));
    fd_release(pinned);

    active_task = &recipient;
    fd_table_close_all(recipient.fd_table);
    active_task = &sender;
    assert(fd_close(source));
    assert(close_count == (int)MAX_FDS - 2);

    recipient.state = TASK_STATE_EXITING;
    assert(fd_duplicate_to(source, recipient.pid, FD_RIGHT_READ) ==
           FD_HANDLE_INVALID);
    recipient.state = TASK_STATE_ZOMBIE;
    assert(fd_duplicate_to(source, recipient.pid, FD_RIGHT_READ) ==
           FD_HANDLE_INVALID);
}

static void test_descriptor_transfer_reservation_and_commit(void) {
    task_t sender, recipient;
    reset_task(&sender, 111);
    reset_task(&recipient, 112);
    active_task = &sender;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    fd_handle_t source = alloc_fd(&read_write_node, 0,
                                 FD_RIGHT_READ | FD_RIGHT_WRITE);
    assert(source != FD_HANDLE_INVALID);
    vfs_open_file_t* pinned = fd_acquire(
        source, FD_RIGHT_READ | FD_RIGHT_WRITE);
    assert(pinned != NULL);
    assert((pinned->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2u);

    fd_transfer_reservation_t reservation = {0};
    assert(fd_transfer_reserve(recipient.pid, FD_RIGHT_READ, &reservation));
    assert(reservation.recipient_pid == recipient.pid);
    assert(reservation.slot == 3u && reservation.generation == 1u);
    assert(reservation.rights == FD_RIGHT_READ);
    assert(recipient.fd_table[3].reserved);
    assert(recipient.fd_table[3].file == NULL);

    /* Reserved capacity is invisible to handles and unavailable to allocs. */
    active_task = &recipient;
    assert(!fd_has_rights(((u64)recipient.pid << 32) |
                          ((u64)reservation.generation << FD_HANDLE_SLOT_BITS) |
                          reservation.slot, FD_RIGHT_READ));
    fd_handle_t other = alloc_fd(&write_only_node, 0, FD_RIGHT_WRITE);
    assert(other != FD_HANDLE_INVALID && FD_HANDLE_SLOT(other) == 4u);

    /* A malformed copy of the reservation cannot publish or consume it. */
    fd_transfer_reservation_t forged = reservation;
    forged.rights |= 0x80u;
    active_task = &sender;
    assert(fd_transfer_commit(&forged, pinned) == FD_HANDLE_INVALID);
    assert(recipient.fd_table[3].reserved);
    assert((pinned->lifecycle & VFS_OPEN_FILE_REF_MASK) == 2u);

    fd_handle_t received = fd_transfer_commit(&reservation, pinned);
    assert(received != FD_HANDLE_INVALID);
    assert(reservation.recipient_pid == 0 && reservation.generation == 0);
    assert((u32)(received >> 32) == recipient.pid);
    assert(FD_HANDLE_SLOT(received) == 3u);
    assert(!recipient.fd_table[3].reserved);

    active_task = &recipient;
    assert(fd_has_rights(received, FD_RIGHT_READ));
    assert(!fd_has_rights(received, FD_RIGHT_WRITE));
    vfs_open_file_t* recipient_file = fd_acquire(received, FD_RIGHT_READ);
    assert(recipient_file == pinned);
    recipient_file->offset = 1234u;
    fd_release(recipient_file);

    active_task = &sender;
    assert(fd_close(source));
    assert(close_count == 0);
    active_task = &recipient;
    recipient_file = fd_acquire(received, FD_RIGHT_READ);
    assert(recipient_file != NULL && recipient_file->offset == 1234u);
    fd_release(recipient_file);
    assert(fd_close(received));
    assert(close_count == 1);
    assert(fd_close(other));
}

static void test_descriptor_transfer_cancel_and_stale_reservations(void) {
    task_t sender, recipient;
    reset_task(&sender, 113);
    reset_task(&recipient, 114);
    active_task = &sender;
    close_count = 0;

    fd_handle_t source = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(source != FD_HANDLE_INVALID);
    vfs_open_file_t* pinned = fd_acquire(source, FD_RIGHT_READ);
    assert(pinned != NULL);

    fd_transfer_reservation_t old_reservation = {0};
    assert(fd_transfer_reserve(recipient.pid, FD_RIGHT_READ,
                               &old_reservation));
    u32 old_generation = old_reservation.generation;
    u32 old_slot = old_reservation.slot;
    fd_transfer_cancel(&old_reservation);
    assert(old_reservation.recipient_pid == 0);
    assert(!recipient.fd_table[old_slot].reserved);

    /* Cancellation burns the token generation; a later allocation cannot be
     * revoked by a stale cancel or committed through the old reservation. */
    active_task = &recipient;
    fd_handle_t replacement = alloc_fd(&write_only_node, 0,
                                       FD_RIGHT_WRITE);
    assert(replacement != FD_HANDLE_INVALID);
    assert(FD_HANDLE_SLOT(replacement) == old_slot);
    assert((u32)((replacement >> FD_HANDLE_SLOT_BITS) &
                 FD_HANDLE_GENERATION_MAX) > old_generation);

    active_task = &sender;
    assert(fd_transfer_commit(&old_reservation, pinned) == FD_HANDLE_INVALID);
    fd_transfer_cancel(&old_reservation);
    active_task = &recipient;
    assert(fd_has_rights(replacement, FD_RIGHT_WRITE));
    fd_release(pinned);
    assert(fd_close(source));
    assert(close_count == 1);

    active_task = &recipient;
    assert(fd_has_rights(replacement, FD_RIGHT_WRITE));
    assert(fd_close(replacement));
}

static void test_descriptor_transfer_reservation_rejects_invalid_and_full(void) {
    task_t sender, recipient;
    vfs_node_t fillers[MAX_FDS];
    reset_task(&sender, 115);
    reset_task(&recipient, 116);
    fd_transfer_reservation_t reservation = {0};

    assert(!fd_transfer_reserve(recipient.pid, 0, &reservation));
    assert(!fd_transfer_reserve(recipient.pid, FD_RIGHT_READ | 0x80u,
                                &reservation));
    recipient.state = TASK_STATE_EXITING;
    assert(!fd_transfer_reserve(recipient.pid, FD_RIGHT_READ, &reservation));
    recipient.state = TASK_STATE_READY;

    active_task = &recipient;
    for (u32 i = 3; i < MAX_FDS; i++) {
        fillers[i] = (vfs_node_t){
            .flags = VFS_FILE, .read = test_read, .close = test_close
        };
        fd_alloc_status_t status = FD_ALLOC_FAILED;
        assert(fd_alloc(&fillers[i], 0, FD_RIGHT_READ, &status) !=
               FD_HANDLE_INVALID);
        assert(status == FD_ALLOC_OK);
    }
    assert(!fd_transfer_reserve(recipient.pid, FD_RIGHT_READ, &reservation));
    fd_table_close_all(recipient.fd_table);
}

static void test_descriptor_transfer_reservation_cancelled_on_recipient_exit(void) {
    task_t sender, recipient;
    reset_task(&sender, 117);
    reset_task(&recipient, 118);
    active_task = &sender;
    close_count = 0;

    fd_handle_t source = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(source != FD_HANDLE_INVALID);
    vfs_open_file_t* pinned = fd_acquire(source, FD_RIGHT_READ);
    assert(pinned != NULL);
    fd_transfer_reservation_t reservation = {0};
    assert(fd_transfer_reserve(recipient.pid, FD_RIGHT_READ, &reservation));

    /* The exit path revokes unpublished reservations together with entries. */
    recipient.state = TASK_STATE_EXITING;
    fd_table_close_all(recipient.fd_table);
    fd_handle_t failed = fd_transfer_commit(&reservation, pinned);
    assert(failed == FD_HANDLE_INVALID);
    assert(recipient.fd_table[reservation.slot].reserved == false);
    fd_transfer_cancel(&reservation);
    assert(reservation.recipient_pid == 0);

    fd_release(pinned);
    assert(fd_close(source));
    assert(close_count == 1);
}

static void test_open_file_allocation_failure_preserves_node_ownership(void) {
    task_t task;
    vfs_node_t node = {
        .flags = VFS_FILE, .read = test_read, .close = test_close
    };
    reset_task(&task, 66);
    active_task = &task;
    close_count = 0;
    fail_next_kmalloc = true;

    fd_alloc_status_t status = FD_ALLOC_OK;
    assert(fd_alloc(&node, 0, FD_RIGHT_READ, &status) == FD_HANDLE_INVALID);
    assert(status == FD_ALLOC_FAILED);
    assert(close_count == 0);

    /* Allocation failure did not claim or destroy the resolved VFS node. */
    fd_handle_t retry = alloc_fd(&node, 0, FD_RIGHT_READ);
    assert(retry != FD_HANDLE_INVALID);
    assert(fd_close(retry));
    assert(close_count == 1);
}

static void test_open_file_pins_outlive_descriptor_revocation(void) {
    task_t task;
    reset_task(&task, 62);
    active_task = &task;
    close_count = 0;

    fd_handle_t handle = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(handle != FD_HANDLE_INVALID);

    vfs_open_file_t* first_pin = fd_acquire(handle, FD_RIGHT_READ);
    vfs_open_file_t* second_pin = fd_acquire(handle, FD_RIGHT_READ);
    assert(first_pin != NULL && second_pin == first_pin);
    assert((first_pin->lifecycle & VFS_OPEN_FILE_REF_MASK) == 3);

    assert(fd_close(handle));
    assert(!fd_has_rights(handle, FD_RIGHT_READ));
    assert(close_count == 0);
    assert(first_pin->node == &read_write_node);

    fd_release(first_pin);
    assert(close_count == 0);
    fd_release(second_pin);
    assert(close_count == 1);
}

typedef struct {
    vfs_open_file_t* file;
    pthread_barrier_t* barrier;
} release_thread_args_t;

static void* concurrent_file_release(void* opaque) {
    release_thread_args_t* args = (release_thread_args_t*)opaque;
    (void)pthread_barrier_wait(args->barrier);
    fd_release(args->file);
    return NULL;
}

static void test_concurrent_final_release_runs_destructor_once(void) {
    task_t task;
    reset_task(&task, 65);
    active_task = &task;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    fd_handle_t handle = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(handle != FD_HANDLE_INVALID);
    vfs_open_file_t* first = fd_acquire(handle, FD_RIGHT_READ);
    vfs_open_file_t* second = fd_acquire(handle, FD_RIGHT_READ);
    assert(first != NULL && first == second);
    assert((first->lifecycle & VFS_OPEN_FILE_REF_MASK) == 3);

    /* Drop the table's reference before racing the two operation pins. */
    assert(fd_close(handle));
    assert(close_count == 0);

    pthread_barrier_t barrier;
    assert(pthread_barrier_init(&barrier, NULL, 3) == 0);
    release_thread_args_t first_args = {.file = first, .barrier = &barrier};
    release_thread_args_t second_args = {.file = second, .barrier = &barrier};
    pthread_t first_thread, second_thread;
    assert(pthread_create(&first_thread, NULL, concurrent_file_release,
                          &first_args) == 0);
    assert(pthread_create(&second_thread, NULL, concurrent_file_release,
                          &second_args) == 0);
    (void)pthread_barrier_wait(&barrier);
    assert(pthread_join(first_thread, NULL) == 0);
    assert(pthread_join(second_thread, NULL) == 0);
    assert(pthread_barrier_destroy(&barrier) == 0);
    assert(__atomic_load_n(&close_count, __ATOMIC_RELAXED) == 1);
}

static void test_independent_opens_have_independent_offsets(void) {
    task_t task;
    vfs_node_t first_node = {
        .flags = VFS_FILE, .read = test_read, .write = test_write,
        .close = test_close
    };
    vfs_node_t second_node = {
        .flags = VFS_FILE, .read = test_read, .write = test_write,
        .close = test_close
    };
    reset_task(&task, 63);
    active_task = &task;
    close_count = 0;

    fd_handle_t first = alloc_fd(&first_node, 0, FD_RIGHT_READ);
    fd_handle_t second = alloc_fd(&second_node, 0, FD_RIGHT_READ);
    assert(first != FD_HANDLE_INVALID && second != FD_HANDLE_INVALID);

    vfs_open_file_t* first_file = fd_acquire(first, FD_RIGHT_READ);
    vfs_open_file_t* second_file = fd_acquire(second, FD_RIGHT_READ);
    assert(first_file && second_file && first_file != second_file);
    first_file->offset = 19;
    assert(second_file->offset == 0);

    fd_release(first_file);
    fd_release(second_file);
    assert(fd_close(first));
    assert(fd_close(second));
    assert(close_count == 2);
}

static void test_table_cleanup_drops_only_its_reference(void) {
    task_t task;
    reset_task(&task, 64);
    active_task = &task;
    close_count = 0;

    fd_handle_t handle = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(handle != FD_HANDLE_INVALID);
    vfs_open_file_t* pin = fd_acquire(handle, FD_RIGHT_READ);
    assert(pin != NULL);

    fd_table_close_all(task.fd_table);
    assert(!fd_has_rights(handle, FD_RIGHT_READ));
    assert(close_count == 0);
    fd_release(pin);
    assert(close_count == 1);
}

typedef struct {
    vfs_open_file_t* file;
} concurrent_release_args_t;

static void* concurrent_release(void* opaque) {
    concurrent_release_args_t* args = (concurrent_release_args_t*)opaque;
    (void)pthread_barrier_wait(&owner_start_barrier);
    fd_release(args->file);
    return NULL;
}

static void test_concurrent_final_release_runs_close_once(void) {
    task_t task;
    reset_task(&task, 65);
    active_task = &task;
    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;

    fd_handle_t handle = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(handle != FD_HANDLE_INVALID);
    vfs_open_file_t* first = fd_acquire(handle, FD_RIGHT_READ);
    vfs_open_file_t* second = fd_acquire(handle, FD_RIGHT_READ);
    assert(first && second && first == second);

    /* Drop the descriptor's ownership first; the two worker pins are now
     * the only references and race to perform the final release. */
    assert(fd_close(handle));
    assert(close_count == 0);

    concurrent_release_args_t a = { .file = first };
    concurrent_release_args_t b = { .file = second };
    pthread_t thread_a, thread_b;
    assert(pthread_barrier_init(&owner_start_barrier, NULL, 3) == 0);
    assert(pthread_create(&thread_a, NULL, concurrent_release, &a) == 0);
    assert(pthread_create(&thread_b, NULL, concurrent_release, &b) == 0);
    (void)pthread_barrier_wait(&owner_start_barrier);
    assert(pthread_join(thread_a, NULL) == 0);
    assert(pthread_join(thread_b, NULL) == 0);
    assert(pthread_barrier_destroy(&owner_start_barrier) == 0);
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
        fd_handle_t handle = alloc_fd(&capacity_nodes[i], 0, FD_RIGHT_READ);
        assert(handle != FD_HANDLE_INVALID);
        assert(FD_HANDLE_SLOT(handle) == i);
    }
    assert(alloc_fd(&read_write_node, 0, FD_RIGHT_READ) == FD_HANDLE_INVALID);
    u32 close_before_failed_allocation = (u32)close_count;
    fd_alloc_status_t failed_status = FD_ALLOC_OK;
    assert(fd_alloc(&read_write_node, 0, FD_RIGHT_READ, &failed_status) ==
           FD_HANDLE_INVALID);
    assert(failed_status == FD_ALLOC_FAILED);
    assert((u32)close_count == close_before_failed_allocation);

    fd_table_close_all(task.fd_table);
    assert(close_count == MAX_FDS - 3);
    for (u32 i = 3; i < MAX_FDS; i++) {
        fd_handle_t stale = ((u64)61 << 32) |
            ((u64)task.fd_table[i].generation << FD_HANDLE_SLOT_BITS) | i;
        assert(!fd_has_rights(stale, FD_RIGHT_READ));
        assert(task.fd_table[i].rights == 0);
        assert(task.fd_table[i].file == NULL);
        assert(task.fd_table[i].generation == 1U);
    }

    /* Slot exhaustion must roll back the node claim without consuming the
     * caller's node. The same node can be allocated successfully afterward. */
    fd_handle_t retry = alloc_fd(&read_write_node, 0, FD_RIGHT_READ);
    assert(retry != FD_HANDLE_INVALID);
    assert(FD_HANDLE_SLOT(retry) == 3);
    assert(task.fd_table[3].generation == 2);
    assert(fd_close(retry));
    assert(close_count == MAX_FDS - 2);
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
    assert(!fd_has_rights(FD_HANDLE_INVALID, FD_RIGHT_READ));

    close_count = 0;
    observed_fd = FD_HANDLE_INVALID;
    fd_alloc_status_t status = FD_ALLOC_FAILED;
    fd_handle_t allocated = fd_alloc(&read_write_node, 0, FD_RIGHT_READ, &status);
    assert(allocated == handle);
    assert(status == FD_ALLOC_OK);
    assert(fd_has_rights(allocated, FD_RIGHT_READ));
    assert(fd_has_rights(allocated, FD_RIGHT_READ));
    assert(fd_close(allocated));
    assert(close_count == 1);
}

typedef struct {
    task_t* task;
    fd_handle_t handle;
    fd_alloc_status_t status;
} concurrent_owner_args_t;

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
    test_descriptor_duplication_shares_file_and_attenuates_rights();
    test_sender_close_races_descriptor_duplication();
    test_recipient_exit_releases_only_its_duplicate_reference();
    test_duplicate_rejects_full_and_exiting_recipients();
    test_descriptor_transfer_reservation_and_commit();
    test_descriptor_transfer_cancel_and_stale_reservations();
    test_descriptor_transfer_reservation_rejects_invalid_and_full();
    test_descriptor_transfer_reservation_cancelled_on_recipient_exit();
    test_concurrent_closeable_node_claim_is_unique();
    test_open_file_allocation_failure_preserves_node_ownership();
    test_open_file_pins_outlive_descriptor_revocation();
    test_concurrent_final_release_runs_destructor_once();
    test_independent_opens_have_independent_offsets();
    test_table_cleanup_drops_only_its_reference();
    test_concurrent_final_release_runs_close_once();
    test_capacity_and_cleanup();
    test_generation_exhaustion_fails_closed();
    test_maximum_identity_descriptor_is_valid();
    puts("PASS: task-bound descriptor handles, stale-reference rejection, rights and lifecycle");
    return 0;
}
