#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <kernel/ipc.h>
#include <kernel/security.h>

static task_t task_a, task_b, task_c;
static task_t* active_task;
task_t* scheduler_current(void) { return active_task; }
void scheduler_yield(void) { }
bool security_check_capability(task_t* task, u32 capability) {
    return task && capability != 0 &&
           (task->capabilities & capability) == capability;
}

void task_unblock(task_t* task) {
    if (task && task->state == TASK_STATE_BLOCKED)
        task->state = (task == active_task) ? TASK_STATE_RUNNING : TASK_STATE_READY;
}

#define IPC_TEST_SOURCE_FD ((fd_handle_t)0xC41A0003ULL)

static vfs_open_file_t transfer_file;
static bool transfer_source_open;
static u32 transfer_source_rights;
static bool transfer_slot_available;
static bool transfer_reservation_live;
static bool transfer_commit_forced_failure;
static fd_transfer_reservation_t transfer_reservation;
static vfs_open_file_t* transfer_received_file;
static fd_handle_t transfer_received_handle;
static u32 transfer_received_rights;
static u32 transfer_final_release_count;

static void transfer_test_reset(void) {
    transfer_file = (vfs_open_file_t){0};
    transfer_file.lifecycle = 1;
    transfer_source_open = true;
    transfer_source_rights = FD_RIGHT_READ | FD_RIGHT_WRITE;
    transfer_slot_available = true;
    transfer_reservation_live = false;
    transfer_commit_forced_failure = false;
    transfer_reservation = (fd_transfer_reservation_t){0};
    transfer_received_file = NULL;
    transfer_received_handle = FD_HANDLE_INVALID;
    transfer_received_rights = 0;
    transfer_final_release_count = 0;
}

vfs_open_file_t* fd_acquire(fd_handle_t handle, u32 required_rights) {
    assert(!ipc_test_irq_disabled());
    if (active_task != &task_a || handle != IPC_TEST_SOURCE_FD ||
        !transfer_source_open || !required_rights ||
        (required_rights & ~FD_RIGHT_MASK) ||
        (transfer_source_rights & required_rights) != required_rights)
        return NULL;
    __atomic_add_fetch(&transfer_file.lifecycle, 1u, __ATOMIC_ACQ_REL);
    return &transfer_file;
}

void fd_release(vfs_open_file_t* file) {
    assert(!ipc_test_irq_disabled());
    assert(file == &transfer_file);
    u64 previous = __atomic_fetch_sub(&file->lifecycle, 1u, __ATOMIC_ACQ_REL);
    assert(previous > 0);
    if (previous == 1u) transfer_final_release_count++;
}

bool fd_transfer_reserve(u32 recipient_pid, u32 rights,
                         fd_transfer_reservation_t* reservation) {
    assert(ipc_test_irq_disabled());
    if (!reservation || recipient_pid != task_b.pid || !rights ||
        (rights & ~FD_RIGHT_MASK) || !transfer_slot_available ||
        transfer_reservation_live)
        return false;
    transfer_slot_available = false;
    transfer_reservation_live = true;
    transfer_reservation = (fd_transfer_reservation_t){
        .recipient_pid = recipient_pid,
        .slot = 3u,
        .generation = 1u,
        .rights = rights
    };
    *reservation = transfer_reservation;
    return true;
}

fd_handle_t fd_transfer_commit(fd_transfer_reservation_t* reservation,
                               vfs_open_file_t* file) {
    assert(ipc_test_irq_disabled());
    if (transfer_commit_forced_failure || !reservation ||
        !transfer_reservation_live || file != &transfer_file ||
        reservation->recipient_pid != transfer_reservation.recipient_pid ||
        reservation->slot != transfer_reservation.slot ||
        reservation->generation != transfer_reservation.generation ||
        reservation->rights != transfer_reservation.rights)
        return FD_HANDLE_INVALID;
    transfer_received_file = file;
    transfer_received_rights = reservation->rights;
    transfer_received_handle =
        ((u64)reservation->recipient_pid << 32) |
        ((u64)reservation->generation << FD_HANDLE_SLOT_BITS) |
        reservation->slot;
    transfer_reservation_live = false;
    *reservation = (fd_transfer_reservation_t){0};
    return transfer_received_handle;
}

void fd_transfer_cancel(fd_transfer_reservation_t* reservation) {
    assert(ipc_test_irq_disabled());
    if (transfer_reservation_live && reservation &&
        reservation->recipient_pid == transfer_reservation.recipient_pid &&
        reservation->slot == transfer_reservation.slot &&
        reservation->generation == transfer_reservation.generation) {
        transfer_reservation_live = false;
        transfer_slot_available = true;
        transfer_reservation = (fd_transfer_reservation_t){0};
    }
    if (reservation) *reservation = (fd_transfer_reservation_t){0};
}

static void transfer_test_close_source(void) {
    assert(transfer_source_open);
    transfer_source_open = false;
    fd_release(&transfer_file);
}

static void transfer_test_close_received(void) {
    assert(transfer_received_file == &transfer_file);
    fd_release(transfer_received_file);
    transfer_received_file = NULL;
    transfer_received_handle = FD_HANDLE_INVALID;
}

static void reset_task(task_t* task, u32 pid) {
    *task = (task_t){0};
    task->pid = pid;
    task->state = TASK_STATE_READY;
    task->scheduler_queued = true;
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        task->ipc_endpoint_generations[i] = 1;
}
static void test_pair_validation(void) {
    ipc_handle_t a = 99, b = 99;
    task_t unpublished = {0};
    unpublished.pid = 808;
    unpublished.state = TASK_STATE_READY;
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        unpublished.ipc_endpoint_generations[i] = 1;
    assert(!ipc_endpoint_pair_create(&task_a, &unpublished,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
    assert(!ipc_endpoint_pair_create(&task_a, &task_b, 0, IPC_RIGHT_RECEIVE, &a, &b));
    assert(a == IPC_HANDLE_INVALID && b == IPC_HANDLE_INVALID);
    assert(!ipc_endpoint_pair_create(&task_a, &task_a, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
    assert(!ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_SEND | 0x80U, IPC_RIGHT_RECEIVE, &a, &b));
    ipc_handle_t aliased = 0;
    assert(!ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_SEND,
                                     IPC_RIGHT_RECEIVE, &aliased, &aliased));
    task_c.is_user = true;
    assert(!ipc_endpoint_pair_create(&task_a, &task_c, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
    task_c.is_user = false;
}
static void test_directional_rights(void) {
    ipc_handle_t receive_a, send_c;
    assert(ipc_endpoint_pair_create(&task_a, &task_c,
        IPC_RIGHT_RECEIVE, IPC_RIGHT_SEND, &receive_a, &send_c));

    active_task = &task_a;
    assert(ipc_send(receive_a, "x", 1) == IPC_STATUS_ERROR);
    assert(ipc_recv(receive_a, NULL, 0, NULL) == IPC_STATUS_EMPTY);

    active_task = &task_c;
    assert(ipc_recv(send_c, NULL, 0, NULL) == IPC_STATUS_ERROR);
    assert(ipc_send(send_c, "y", 1) == IPC_STATUS_OK);

    active_task = &task_c;
    assert(ipc_close(send_c) == IPC_STATUS_OK);

    active_task = &task_a;
    char received = 0;
    u32 sender = 0;
    assert(ipc_recv(receive_a, &received, sizeof(received), &sender) == 1);
    assert(received == 'y' && sender == task_c.pid);
    /* A receive-only endpoint can distinguish an empty queue from EOF after
     * the peer closes, even when the peer delivered a final queued message. */
    assert(ipc_recv(receive_a, &received, sizeof(received), &sender) ==
           IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(receive_a) == IPC_STATUS_OK);
}

static void test_waiter_message_wakeup(void) {
    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &send_a, &receive_b));
    active_task = &task_b;
    assert(ipc_test_register_waiter(receive_b));
    assert(task_b.state == TASK_STATE_BLOCKED);
    assert(task_b.waiting_event == TASK_WAITING_IPC);
    assert(task_b.ipc_wait_endpoint != NULL);
    assert(task_b.ipc_wait_status == IPC_WAIT_STATUS_PENDING);

    active_task = &task_a;
    const char message[] = "wake";
    assert(ipc_send(send_a, message, sizeof(message)) == IPC_STATUS_OK);
    assert(task_b.state == TASK_STATE_READY);
    assert(task_b.waiting_event == 0);
    assert(task_b.ipc_wait_endpoint == NULL && task_b.ipc_wait_next == NULL);
    assert(task_b.ipc_wait_status == IPC_WAIT_STATUS_MESSAGE);

    active_task = &task_b;
    char received[sizeof(message)] = {0};
    assert(ipc_recv(receive_b, received, sizeof(received), NULL) ==
           (int)sizeof(message));
    assert(memcmp(received, message, sizeof(message)) == 0);
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
}

static void test_waiter_spurious_wakeup(void) {
    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &send_a, &receive_b));
    active_task = &task_b;
    assert(ipc_test_register_waiter(receive_b));
    assert(ipc_test_wake_waiters(receive_b, IPC_WAIT_STATUS_MESSAGE));
    assert(task_b.state == TASK_STATE_RUNNING);
    assert(task_b.ipc_wait_endpoint == NULL && task_b.waiting_event == 0);
    assert(ipc_recv(receive_b, NULL, 0, NULL) == IPC_STATUS_EMPTY);

    /* A spurious wake did not consume a message or poison the next wait. */
    assert(ipc_test_register_waiter(receive_b));
    active_task = &task_a;
    assert(ipc_send(send_a, "z", 1) == IPC_STATUS_OK);
    assert(task_b.state == TASK_STATE_READY);
    active_task = &task_b;
    char byte = 0;
    assert(ipc_recv(receive_b, &byte, sizeof(byte), NULL) == 1);
    assert(byte == 'z');
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
}

static void test_waiter_peer_close_wakeup(void) {
    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &send_a, &receive_b));
    active_task = &task_b;
    assert(ipc_test_register_waiter(receive_b));
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
    assert(task_b.state == TASK_STATE_READY);
    assert(task_b.ipc_wait_endpoint == NULL && task_b.waiting_event == 0);
    assert(task_b.ipc_wait_status == IPC_WAIT_STATUS_PEER_CLOSED);
    active_task = &task_b;
    assert(ipc_recv(receive_b, NULL, 0, NULL) == IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
}

static void test_waiter_cancelled_on_task_teardown(void) {
    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &send_a, &receive_b));
    active_task = &task_b;
    assert(ipc_test_register_waiter(receive_b));
    active_task = &task_a;
    ipc_task_close_all(&task_b);
    assert(task_b.ipc_wait_endpoint == NULL && task_b.ipc_wait_next == NULL);
    assert(task_b.waiting_event == 0);
    assert(task_b.ipc_wait_status == IPC_WAIT_STATUS_CANCELLED);
    assert(task_b.state == TASK_STATE_READY);
    assert(ipc_send(send_a, "x", 1) == IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(send_a) == IPC_STATUS_OK);
}

static void test_service_registration_policy_and_accept(void) {
    reset_task(&task_a, 101);
    reset_task(&task_b, 202);
    reset_task(&task_c, 303);
    task_a.capabilities = CAP_IPC_SERVICE;
    task_b.capabilities = CAP_FS_READ;

    active_task = &task_a;
    ipc_service_handle_t service = 0;
    assert(ipc_service_register("com.charis.echo", CAP_FS_READ,
                                &service) == IPC_STATUS_OK);
    assert(service != 0);
    /* A second subsystem init must not revive old service/request generations. */
    ipc_init();
    ipc_request_handle_t self_request = 0;
    assert(ipc_service_connect_begin("com.charis.echo", IPC_RIGHT_SEND,
                                     &self_request) == IPC_STATUS_ACCESS_DENIED);
    assert(self_request == 0);
    ipc_service_handle_t duplicate = 0;
    assert(ipc_service_register("com.charis.echo", 0, &duplicate) ==
           IPC_STATUS_SERVICE_EXISTS);
    assert(ipc_service_register("bad name", 0, &duplicate) == IPC_STATUS_ERROR);
    assert(ipc_service_register("", 0, &duplicate) == IPC_STATUS_ERROR);
    assert(ipc_service_register("com.charis.invalid", 1U << 31,
                                &duplicate) == IPC_STATUS_ERROR);

    active_task = &task_c;
    ipc_request_handle_t request = 0;
    assert(ipc_service_connect_begin("com.charis.echo", IPC_RIGHT_SEND,
                                     &request) == IPC_STATUS_ACCESS_DENIED);
    assert(request == 0);

    active_task = &task_a;
    assert(ipc_test_service_register_waiter(service));
    assert(task_a.state == TASK_STATE_BLOCKED);
    assert(task_a.waiting_event == TASK_WAITING_IPC_SERVICE);

    active_task = &task_b;
    assert(ipc_service_connect_begin("com.charis.echo", IPC_RIGHT_MASK,
                                     &request) == IPC_STATUS_OK);
    assert(request != 0);
    assert(task_a.state == TASK_STATE_READY);
    assert(task_a.ipc_service_wait == NULL);
    assert(task_a.waiting_event == 0);

    active_task = &task_a;
    ipc_request_handle_t offered = 0;
    u32 client_pid = 0;
    assert(ipc_service_accept_wait(service, &offered, &client_pid) ==
           IPC_STATUS_OK);
    /* Client and server receive different PID-bound request handles. */
    assert(offered != request && client_pid == task_b.pid);
    assert((u32)(request >> 32) == task_b.pid);
    assert((u32)(offered >> 32) == task_a.pid);

    ipc_handle_t server_endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_accept(service, offered, IPC_RIGHT_SEND,
        IPC_RIGHT_RECEIVE, &server_endpoint) == IPC_STATUS_OK);
    assert(server_endpoint != IPC_HANDLE_INVALID);

    active_task = &task_b;
    ipc_handle_t client_endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_connect_wait(request, &client_endpoint) == IPC_STATUS_OK);
    assert(client_endpoint != IPC_HANDLE_INVALID);

    const char payload[] = "service-rendezvous";
    assert(ipc_send(client_endpoint, payload, sizeof(payload)) == IPC_STATUS_OK);
    assert(ipc_recv(client_endpoint, (void*)payload, sizeof(payload), NULL) ==
           IPC_STATUS_ERROR);

    active_task = &task_a;
    char received[sizeof(payload)] = {0};
    u32 sender_pid = 0;
    assert(ipc_recv(server_endpoint, received, sizeof(received), &sender_pid) ==
           (int)sizeof(payload));
    assert(sender_pid == task_b.pid);
    assert(memcmp(received, payload, sizeof(payload)) == 0);
    assert(ipc_send(server_endpoint, payload, sizeof(payload)) == IPC_STATUS_ERROR);

    assert(ipc_close(server_endpoint) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_close(client_endpoint) == IPC_STATUS_OK);
    active_task = &task_a;
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
    assert(ipc_service_unregister(service) == IPC_STATUS_ERROR);
}

static void test_service_revalidates_client_capabilities_at_accept(void) {
    reset_task(&task_a, 131);
    reset_task(&task_b, 242);
    task_a.capabilities = CAP_IPC_SERVICE;
    task_b.capabilities = CAP_FS_READ;

    active_task = &task_a;
    ipc_service_handle_t service = 0;
    assert(ipc_service_register("com.charis.policy", CAP_FS_READ,
                                &service) == IPC_STATUS_OK);

    active_task = &task_b;
    ipc_request_handle_t request = 0;
    assert(ipc_service_connect_begin("com.charis.policy", IPC_RIGHT_SEND,
                                     &request) == IPC_STATUS_OK);

    /* Authority can be reduced while the request is pending. Acceptance must
     * re-check the live TCB rather than relying only on connect-time policy. */
    task_b.capabilities = 0;
    active_task = &task_a;
    ipc_request_handle_t offered = 0;
    assert(ipc_service_accept_wait(service, &offered, NULL) == IPC_STATUS_OK);
    ipc_handle_t server_endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_accept(service, offered, IPC_RIGHT_SEND,
        IPC_RIGHT_RECEIVE, &server_endpoint) == IPC_STATUS_ACCESS_DENIED);
    assert(server_endpoint == IPC_HANDLE_INVALID);

    active_task = &task_b;
    ipc_handle_t client_endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_connect_wait(request, &client_endpoint) ==
           IPC_STATUS_ACCESS_DENIED);
    assert(client_endpoint == IPC_HANDLE_INVALID);

    active_task = &task_a;
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
}

static void test_service_reject_and_pending_client_exit(void) {
    reset_task(&task_a, 111);
    reset_task(&task_b, 222);
    task_a.capabilities = CAP_IPC_SERVICE;
    task_b.capabilities = CAP_FS_READ;

    active_task = &task_a;
    ipc_service_handle_t service = 0;
    assert(ipc_service_register("com.charis.reject", CAP_FS_READ,
                                &service) == IPC_STATUS_OK);

    active_task = &task_b;
    ipc_request_handle_t request = 0;
    assert(ipc_service_connect_begin("com.charis.reject", IPC_RIGHT_SEND,
                                     &request) == IPC_STATUS_OK);
    active_task = &task_a;
    ipc_request_handle_t offered = 0;
    assert(ipc_service_accept_wait(service, &offered, NULL) == IPC_STATUS_OK);
    assert(ipc_service_reject(service, offered) == IPC_STATUS_OK);

    active_task = &task_b;
    ipc_handle_t endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_connect_wait(request, &endpoint) ==
           IPC_STATUS_REQUEST_REJECTED);
    assert(endpoint == IPC_HANDLE_INVALID);

    request = 0;
    assert(ipc_service_connect_begin("com.charis.reject", IPC_RIGHT_SEND,
                                     &request) == IPC_STATUS_OK);
    task_b.state = TASK_STATE_EXITING;
    ipc_task_close_all(&task_b);
    assert(task_b.ipc_connect_request == NULL);
    assert(task_b.ipc_connect_request_id == 0);

    active_task = &task_a;
    assert(ipc_test_service_dequeue(service, &offered, NULL) == false);
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
}

static void test_service_unregister_wakes_client_and_acceptor(void) {
    reset_task(&task_a, 121);
    reset_task(&task_b, 232);
    task_a.capabilities = CAP_IPC_SERVICE;
    task_b.capabilities = CAP_FS_READ;

    active_task = &task_a;
    ipc_service_handle_t service = 0;
    assert(ipc_service_register("com.charis.close", CAP_FS_READ,
                                &service) == IPC_STATUS_OK);
    assert(ipc_test_service_register_waiter(service));
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
    assert(task_a.state == TASK_STATE_RUNNING);
    assert(task_a.ipc_service_wait == NULL);
    assert(task_a.ipc_service_wait_status == IPC_STATUS_SERVICE_CLOSED);

    active_task = &task_a;
    ipc_service_handle_t replacement = 0;
    assert(ipc_service_register("com.charis.close", 0, &replacement) ==
           IPC_STATUS_OK);
    active_task = &task_b;
    ipc_request_handle_t request = 0;
    assert(ipc_service_connect_begin("com.charis.close", IPC_RIGHT_SEND,
                                     &request) == IPC_STATUS_OK);
    active_task = &task_a;
    assert(ipc_service_unregister(replacement) == IPC_STATUS_OK);
    active_task = &task_b;
    ipc_handle_t endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_connect_wait(request, &endpoint) == IPC_STATUS_SERVICE_CLOSED);
    assert(endpoint == IPC_HANDLE_INVALID);
}

static void test_service_request_table_capacity_and_generation_exhaustion(void) {
    static task_t clients[IPC_MAX_CONNECTIONS + 1U];
    reset_task(&task_a, 401);
    reset_task(&task_b, 402);
    task_a.capabilities = task_b.capabilities = CAP_IPC_SERVICE;

    active_task = &task_a;
    ipc_service_handle_t service_a = 0;
    assert(ipc_service_register("svc.a", 0, &service_a) == IPC_STATUS_OK);
    active_task = &task_b;
    ipc_service_handle_t service_b = 0;
    assert(ipc_service_register("svc.b", 0, &service_b) == IPC_STATUS_OK);

    for (u32 i = 0; i < IPC_MAX_CONNECTIONS + 1U; i++) {
        reset_task(&clients[i], 500 + i);
        active_task = &clients[i];
        ipc_request_handle_t request = 0;
        const char* name = i < IPC_MAX_PENDING_PER_SERVICE ? "svc.a" : "svc.b";
        int result = ipc_service_connect_begin(name, IPC_RIGHT_SEND, &request);
        if (i < IPC_MAX_CONNECTIONS) assert(result == IPC_STATUS_OK);
        else assert(result == IPC_STATUS_SERVICE_FULL);
    }

    active_task = &task_a;
    assert(ipc_service_unregister(service_a) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_service_unregister(service_b) == IPC_STATUS_OK);
    assert(ipc_test_service_generation_exhaustion());
    assert(ipc_test_request_generation_exhaustion());
}

static void test_rights_queue_and_close(void) {
    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_SEND, IPC_RIGHT_SEND | IPC_RIGHT_RECEIVE, &send_a, &receive_b));
    assert((u32)(send_a >> 32) == task_a.pid);
    assert((u32)(receive_b >> 32) == task_b.pid);
    const char payload[] = "charis-ipc";
    active_task = &task_b;
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_ERROR);
    assert(ipc_recv(send_a, (void*)payload, sizeof(payload), NULL) == IPC_STATUS_ERROR);
    assert(ipc_recv(receive_b, (void*)payload, sizeof(payload), NULL) == IPC_STATUS_EMPTY);
    assert(ipc_close(send_a) == IPC_STATUS_ERROR);
    active_task = &task_a;
    assert(ipc_recv(send_a, (void*)payload, sizeof(payload), NULL) == IPC_STATUS_ERROR);
    assert(ipc_send(send_a, payload, IPC_MAX_MSG_SIZE + 1U) == IPC_STATUS_ERROR);
    assert(ipc_send(send_a, NULL, 1) == IPC_STATUS_ERROR);
    for (u32 i = 0; i < IPC_MAX_MESSAGES; i++)
        assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_OK);
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_FULL);
    active_task = &task_b;
    u8 small[2]; u32 sender = 0;
    assert(ipc_recv(receive_b, small, sizeof(small), &sender) == IPC_STATUS_BUFFER_SMALL);
    assert(sender == 0);
    char received[sizeof(payload)];
    for (u32 i = 0; i < IPC_MAX_MESSAGES; i++) {
        memset(received, 0, sizeof(received));
        assert(ipc_recv(receive_b, received, sizeof(received), &sender) == (int)sizeof(payload));
        assert(sender == task_a.pid && memcmp(received, payload, sizeof(payload)) == 0);
    }
    assert(ipc_recv(receive_b, received, sizeof(received), NULL) == IPC_STATUS_EMPTY);
    active_task = &task_a;
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_OK);
    assert(ipc_close(send_a) == IPC_STATUS_OK);
    assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_ERROR);
    active_task = &task_b;
    assert(ipc_recv(receive_b, received, sizeof(received), &sender) == (int)sizeof(payload));
    assert(sender == task_a.pid);
    assert(ipc_send(receive_b, payload, sizeof(payload)) == IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
    assert(ipc_close(receive_b) == IPC_STATUS_ERROR);
}
static void test_stale_handle_and_teardown(void) {
    ipc_handle_t stale, peer;
    assert(ipc_endpoint_pair_create(&task_a, &task_c, IPC_RIGHT_MASK, IPC_RIGHT_MASK, &stale, &peer));
    active_task = &task_a;
    assert(ipc_close(stale) == IPC_STATUS_OK);
    ipc_handle_t fresh, fresh_peer;
    assert(ipc_endpoint_pair_create(&task_a, &task_b, IPC_RIGHT_MASK, IPC_RIGHT_MASK, &fresh, &fresh_peer));
    assert(fresh != stale);
    assert(ipc_send(stale, "x", 1) == IPC_STATUS_ERROR);
    assert(ipc_close(stale) == IPC_STATUS_ERROR);
    ipc_task_close_all(&task_a);
    assert(task_a.ipc_endpoints[0] == NULL && task_a.ipc_endpoints[1] == NULL);
    assert(ipc_send(fresh, "x", 1) == IPC_STATUS_ERROR);
    active_task = &task_b;
    assert(ipc_send(fresh_peer, "x", 1) == IPC_STATUS_PEER_CLOSED);
    assert(ipc_close(fresh_peer) == IPC_STATUS_OK);
    active_task = &task_c;
    assert(ipc_close(peer) == IPC_STATUS_OK);
}
static void test_generation_exhaustion(void) {
    task_t retired, peer;
    reset_task(&retired, 950);
    reset_task(&peer, 951);
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++)
        retired.ipc_endpoint_generations[i] = IPC_HANDLE_GENERATION_MAX;
    ipc_handle_t a, b;
    assert(!ipc_endpoint_pair_create(&retired, &peer,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &a, &b));
}

static void test_table_limit(void) {
    task_t owner, peers[IPC_MAX_ENDPOINTS], extra;
    ipc_handle_t own[IPC_MAX_ENDPOINTS], peer[IPC_MAX_ENDPOINTS];
    reset_task(&owner, 401);
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++) {
        reset_task(&peers[i], 500+i);
        assert(ipc_endpoint_pair_create(&owner, &peers[i], IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &own[i], &peer[i]));
    }
    reset_task(&extra, 900);
    ipc_handle_t h1, h2;
    assert(!ipc_endpoint_pair_create(&owner, &extra, IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE, &h1, &h2));
    ipc_task_close_all(&owner);
    for (u32 i = 0; i < IPC_MAX_ENDPOINTS; i++) ipc_task_close_all(&peers[i]);
}
static void test_fd_attachment_transfer_and_receiver_retry(void) {
    reset_task(&task_a, 1201);
    reset_task(&task_b, 1202);
    transfer_test_reset();

    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND | IPC_RIGHT_TRANSFER,
        IPC_RIGHT_RECEIVE | IPC_RIGHT_TRANSFER, &send_a, &receive_b));

    const char payload[] = "file-attachment";
    active_task = &task_a;
    assert(ipc_send_with_fd(send_a, payload, sizeof(payload),
                            IPC_TEST_SOURCE_FD, FD_RIGHT_READ) == IPC_STATUS_OK);
    assert(transfer_file.lifecycle == 2u);
    assert(transfer_received_file == NULL);

    /* The sender may close its descriptor immediately. The queue now owns
     * the only remaining reference until delivery or queue teardown. */
    transfer_test_close_source();
    assert(transfer_file.lifecycle == 1u);

    active_task = &task_b;
    char received[sizeof(payload)];
    memset(received, 0x7B, sizeof(received));
    u32 sender_pid = 0;
    assert(ipc_recv(receive_b, received, sizeof(received), &sender_pid) ==
           IPC_STATUS_ATTACHMENT_PENDING);
    assert(sender_pid == 0 && (u8)received[0] == 0x7Bu);
    assert(transfer_file.lifecycle == 1u);

    fd_handle_t received_fd = FD_HANDLE_INVALID;
    transfer_slot_available = false;
    assert(ipc_recv_with_fd(receive_b, received, sizeof(received),
                            &sender_pid, &received_fd) ==
           IPC_STATUS_DESCRIPTOR_TABLE_FULL);
    assert(received_fd == FD_HANDLE_INVALID);
    assert(sender_pid == 0 && (u8)received[0] == 0x7Bu);
    assert(transfer_file.lifecycle == 1u);

    /* A failed descriptor commit cancels its slot reservation but leaves the
     * original message and queue-owned reference available for retry. */
    transfer_slot_available = true;
    transfer_commit_forced_failure = true;
    assert(ipc_recv_with_fd(receive_b, received, sizeof(received),
                            &sender_pid, &received_fd) ==
           IPC_STATUS_ATTACHMENT_FAILED);
    assert(transfer_slot_available && !transfer_reservation_live);
    assert(received_fd == FD_HANDLE_INVALID && sender_pid == 0);
    assert(transfer_file.lifecycle == 1u);

    transfer_commit_forced_failure = false;
    memset(received, 0, sizeof(received));
    assert(ipc_recv_with_fd_wait(receive_b, received, sizeof(received),
                                 &sender_pid, &received_fd) ==
           (int)sizeof(payload));
    assert(sender_pid == task_a.pid);
    assert(memcmp(received, payload, sizeof(payload)) == 0);
    assert(received_fd == transfer_received_handle);
    assert(transfer_received_file == &transfer_file);
    assert(transfer_received_rights == FD_RIGHT_READ);
    assert(transfer_file.lifecycle == 1u);

    assert(ipc_recv(receive_b, received, sizeof(received), NULL) ==
           IPC_STATUS_EMPTY);
    transfer_test_close_received();
    assert(transfer_file.lifecycle == 0u);
    assert(transfer_final_release_count == 1u);

    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
}

static void test_fd_attachment_requires_transfer_rights(void) {
    reset_task(&task_a, 1211);
    reset_task(&task_b, 1212);
    transfer_test_reset();

    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND, IPC_RIGHT_RECEIVE | IPC_RIGHT_TRANSFER,
        &send_a, &receive_b));
    const char payload[] = "denied";

    active_task = &task_a;
    assert(ipc_send_with_fd(send_a, payload, sizeof(payload),
                            IPC_TEST_SOURCE_FD, FD_RIGHT_READ) ==
           IPC_STATUS_ERROR);
    assert(transfer_file.lifecycle == 1u);

    transfer_test_close_source();
    assert(transfer_file.lifecycle == 0u);
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
}

static void test_fd_attachment_receiver_authority_and_close_cleanup(void) {
    reset_task(&task_a, 1221);
    reset_task(&task_b, 1222);
    transfer_test_reset();

    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND | IPC_RIGHT_TRANSFER, IPC_RIGHT_RECEIVE,
        &send_a, &receive_b));
    const char payload[] = "must-remain-queued";

    active_task = &task_a;
    assert(ipc_send_with_fd(send_a, payload, sizeof(payload),
                            IPC_TEST_SOURCE_FD, FD_RIGHT_READ) == IPC_STATUS_OK);
    transfer_test_close_source();
    assert(transfer_file.lifecycle == 1u);

    active_task = &task_b;
    char received[sizeof(payload)];
    fd_handle_t received_fd = FD_HANDLE_INVALID;
    assert(ipc_recv_with_fd(receive_b, received, sizeof(received), NULL,
                            &received_fd) == IPC_STATUS_ACCESS_DENIED);
    assert(received_fd == FD_HANDLE_INVALID);
    assert(transfer_file.lifecycle == 1u);

    /* Closing the receiver's inbound queue drops the queued pin after leaving
     * the IPC critical section; fd_release asserts that ordering in this test. */
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
    assert(transfer_file.lifecycle == 0u);
    assert(transfer_final_release_count == 1u);
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
}

static void test_fd_attachment_send_failure_and_task_exit_release(void) {
    reset_task(&task_a, 1231);
    reset_task(&task_b, 1232);
    transfer_test_reset();

    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND | IPC_RIGHT_TRANSFER,
        IPC_RIGHT_RECEIVE | IPC_RIGHT_TRANSFER, &send_a, &receive_b));
    const char payload[] = "queued";

    active_task = &task_a;
    for (u32 i = 0; i < IPC_MAX_MESSAGES; i++)
        assert(ipc_send(send_a, payload, sizeof(payload)) == IPC_STATUS_OK);
    assert(ipc_send_with_fd(send_a, payload, sizeof(payload),
                            IPC_TEST_SOURCE_FD, FD_RIGHT_READ) == IPC_STATUS_FULL);
    assert(transfer_file.lifecycle == 1u);
    transfer_test_close_source();
    assert(transfer_file.lifecycle == 0u);
    assert(transfer_final_release_count == 1u);

    /* A receiver exiting with an unconsumed attachment closes its queue and
     * releases every queue-owned open-file reference before TCB reuse. */
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_close(receive_b) == IPC_STATUS_OK);
}

static void test_fd_attachment_task_exit_releases_queue_pin(void) {
    reset_task(&task_a, 1241);
    reset_task(&task_b, 1242);
    transfer_test_reset();

    ipc_handle_t send_a, receive_b;
    assert(ipc_endpoint_pair_create(&task_a, &task_b,
        IPC_RIGHT_SEND | IPC_RIGHT_TRANSFER,
        IPC_RIGHT_RECEIVE | IPC_RIGHT_TRANSFER, &send_a, &receive_b));
    const char payload[] = "task-exit";

    active_task = &task_a;
    assert(ipc_send_with_fd(send_a, payload, sizeof(payload),
                            IPC_TEST_SOURCE_FD, FD_RIGHT_READ) == IPC_STATUS_OK);
    transfer_test_close_source();
    assert(transfer_file.lifecycle == 1u);

    ipc_task_close_all(&task_b);
    assert(transfer_file.lifecycle == 0u);
    assert(transfer_final_release_count == 1u);
    active_task = &task_a;
    assert(ipc_close(send_a) == IPC_STATUS_OK);
}

int main(void) {
    reset_task(&task_a, 101); reset_task(&task_b, 202); reset_task(&task_c, 303);
    ipc_init();
    test_pair_validation();
    test_directional_rights();
    test_waiter_message_wakeup();
    test_waiter_spurious_wakeup();
    test_waiter_peer_close_wakeup();
    test_waiter_cancelled_on_task_teardown();
    test_service_registration_policy_and_accept();
    test_service_revalidates_client_capabilities_at_accept();
    test_service_reject_and_pending_client_exit();
    test_service_unregister_wakes_client_and_acceptor();
    test_service_request_table_capacity_and_generation_exhaustion();
    test_rights_queue_and_close();
    test_stale_handle_and_teardown();
    test_generation_exhaustion();
    test_table_limit();
    test_fd_attachment_transfer_and_receiver_retry();
    test_fd_attachment_requires_transfer_rights();
    test_fd_attachment_receiver_authority_and_close_cleanup();
    test_fd_attachment_send_failure_and_task_exit_release();
    test_fd_attachment_task_exit_releases_queue_pin();
    puts("PASS: task-owned IPC endpoints, transactional file attachments and teardown");
    return 0;
}
