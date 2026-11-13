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
    return task && capability && !(capability & ~((u32)CAP_ALL)) &&
           (task->capabilities & capability) == capability;
}

void task_unblock(task_t* task) {
    if (task && task->state == TASK_STATE_BLOCKED)
        task->state = (task == active_task) ? TASK_STATE_RUNNING : TASK_STATE_READY;
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
static void test_service_registration_and_policy(void) {
    ipc_service_handle_t service = 0;
    active_task = &task_a;
    task_a.capabilities = 0;
    assert(ipc_service_register("com.charis.echo", 0,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_ERROR);
    assert(ipc_service_register("com.charis.unknown-cap", 1U << 31,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_ERROR);
    assert(ipc_service_register("bad name", 1,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_ERROR);
    assert(ipc_service_register("", 1,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_ERROR);
    assert(ipc_service_register("com.charis.echo", 1,
        0, IPC_RIGHT_MASK, &service) == IPC_STATUS_ERROR);
    assert(ipc_service_register("com.charis.echo", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_OK);
    assert(service != 0);
    /* Re-entering subsystem init must not reset live service generations. */
    ipc_init();
    ipc_service_handle_t duplicate = 0;
    assert(ipc_service_register("com.charis.echo", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &duplicate) == IPC_STATUS_ERROR);

    active_task = &task_b;
    task_b.capabilities = CAP_FS_READ;
    ipc_request_handle_t request = 0xFFFFFFFFFFFFFFFFULL;
    assert(ipc_service_connect("com.charis.echo", &request) ==
           IPC_STATUS_DENIED);
    assert(request == 0);
    task_b.capabilities = CAP_FS_CREATE;
    assert(ipc_service_connect("com.charis.echo", &request) == IPC_STATUS_OK);
    ipc_handle_t client_endpoint = 99;
    assert(ipc_service_result(request, &client_endpoint) == IPC_STATUS_EMPTY);
    assert(client_endpoint == IPC_HANDLE_INVALID);

    active_task = &task_c;
    ipc_request_handle_t offered = 0;
    u32 client_pid = 0;
    assert(ipc_service_next(service, &offered, &client_pid) == IPC_STATUS_ERROR);
    assert(ipc_service_result(request, &client_endpoint) == IPC_STATUS_ERROR);

    active_task = &task_a;
    assert(ipc_service_next(service, &offered, &client_pid) == IPC_STATUS_OK);
    assert(offered == request && client_pid == task_b.pid);

    active_task = &task_c;
    ipc_handle_t forbidden_endpoint = 0;
    assert(ipc_service_accept(service, offered, &forbidden_endpoint) ==
           IPC_STATUS_ERROR);
    assert(forbidden_endpoint == IPC_HANDLE_INVALID);

    active_task = &task_a;
    ipc_handle_t server_endpoint = IPC_HANDLE_INVALID;
    assert(ipc_service_accept(service, offered, &server_endpoint) ==
           IPC_STATUS_OK);
    assert(server_endpoint != IPC_HANDLE_INVALID);

    active_task = &task_b;
    assert(ipc_service_result(request, &client_endpoint) == IPC_STATUS_OK);
    assert(client_endpoint != IPC_HANDLE_INVALID);
    assert(ipc_service_result(request, &client_endpoint) == IPC_STATUS_ERROR);

    /* Service policy, not the client, assigns endpoint rights. */
    const char payload[] = "service-rendezvous";
    active_task = &task_b;
    assert(ipc_send(client_endpoint, payload, sizeof(payload)) == IPC_STATUS_OK);
    active_task = &task_a;
    char received[sizeof(payload)] = {0};
    u32 sender = 0;
    assert(ipc_recv(server_endpoint, received, sizeof(received), &sender) ==
           (int)sizeof(payload));
    assert(sender == task_b.pid &&
           memcmp(received, payload, sizeof(payload)) == 0);
    assert(ipc_close(server_endpoint) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_close(client_endpoint) == IPC_STATUS_OK);

    active_task = &task_a;
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
    assert(ipc_service_unregister(service) == IPC_STATUS_ERROR);
    ipc_service_handle_t replacement = 0;
    assert(ipc_service_register("com.charis.echo", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &replacement) == IPC_STATUS_OK);
    assert(replacement != service);
    assert(ipc_service_next(service, &offered, &client_pid) == IPC_STATUS_ERROR);
    assert(ipc_service_unregister(replacement) == IPC_STATUS_OK);
}

static void test_service_rejection_and_unregister_pending(void) {
    active_task = &task_a;
    task_a.capabilities = 0;
    ipc_service_handle_t service = 0;
    assert(ipc_service_register("com.charis.reject", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_OK);

    active_task = &task_b;
    task_b.capabilities = CAP_FS_CREATE;
    ipc_request_handle_t request = 0;
    assert(ipc_service_connect("com.charis.reject", &request) == IPC_STATUS_OK);
    active_task = &task_a;
    ipc_request_handle_t offered = 0;
    u32 client_pid = 0;
    assert(ipc_service_next(service, &offered, &client_pid) == IPC_STATUS_OK);
    assert(client_pid == task_b.pid);
    assert(ipc_service_reject(service, offered) == IPC_STATUS_OK);

    active_task = &task_b;
    ipc_handle_t endpoint = 99;
    assert(ipc_service_result(request, &endpoint) == IPC_STATUS_REJECTED);
    assert(endpoint == IPC_HANDLE_INVALID);
    assert(ipc_service_result(request, &endpoint) == IPC_STATUS_ERROR);

    request = 0;
    assert(ipc_service_connect("com.charis.reject", &request) == IPC_STATUS_OK);
    active_task = &task_a;
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
    active_task = &task_b;
    assert(ipc_service_result(request, &endpoint) == IPC_STATUS_REJECTED);

    active_task = &task_a;
    ipc_service_handle_t new_service = 0;
    assert(ipc_service_register("com.charis.reject", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &new_service) == IPC_STATUS_OK);
    assert(new_service != service);
    assert(ipc_service_unregister(new_service) == IPC_STATUS_OK);
}

static void test_service_request_lifecycle_on_task_exit(void) {
    active_task = &task_a;
    ipc_service_handle_t service = 0;
    assert(ipc_service_register("com.charis.exit", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_OK);

    active_task = &task_b;
    task_b.capabilities = CAP_FS_CREATE;
    ipc_request_handle_t pending = 0;
    assert(ipc_service_connect("com.charis.exit", &pending) == IPC_STATUS_OK);
    /* A client exiting drops its pending request before its TCB can be reused. */
    ipc_task_close_all(&task_b);
    active_task = &task_a;
    ipc_request_handle_t next = 0;
    u32 client_pid = 0;
    assert(ipc_service_next(service, &next, &client_pid) == IPC_STATUS_EMPTY);
    active_task = &task_b;
    ipc_handle_t endpoint = 0;
    assert(ipc_service_result(pending, &endpoint) == IPC_STATUS_ERROR);

    /* If a client is already terminal before its normal close pass reaches
     * the namespace, the server scan must still reclaim its request slot. */
    active_task = &task_b;
    ipc_request_handle_t dead_client_request = 0;
    assert(ipc_service_connect("com.charis.exit", &dead_client_request) ==
           IPC_STATUS_OK);
    task_b.state = TASK_STATE_EXITING;
    active_task = &task_a;
    assert(ipc_service_next(service, &next, &client_pid) == IPC_STATUS_EMPTY);
    task_b.state = TASK_STATE_READY;
    active_task = &task_b;
    assert(ipc_service_result(dead_client_request, &endpoint) == IPC_STATUS_ERROR);

    /* A server exiting revokes its namespace entries and rejects pending work. */
    active_task = &task_c;
    task_c.capabilities = CAP_FS_CREATE;
    ipc_request_handle_t pending_for_shutdown = 0;
    assert(ipc_service_connect("com.charis.exit", &pending_for_shutdown) ==
           IPC_STATUS_OK);
    ipc_task_close_all(&task_a);
    active_task = &task_c;
    assert(ipc_service_result(pending_for_shutdown, &endpoint) ==
           IPC_STATUS_REJECTED);
    active_task = &task_a;
    assert(ipc_service_next(service, &next, &client_pid) == IPC_STATUS_ERROR);
}

static void test_service_and_request_table_limits(void) {
    active_task = &task_a;
    ipc_service_handle_t services[IPC_MAX_SERVICES];
    char name[IPC_MAX_SERVICE_NAME] = "s00";
    for (u32 i = 0; i < IPC_MAX_SERVICES; i++) {
        name[1] = (char)('0' + (i / 10U));
        name[2] = (char)('0' + (i % 10U));
        assert(ipc_service_register(name, CAP_FS_CREATE, IPC_RIGHT_MASK,
            IPC_RIGHT_MASK, &services[i]) == IPC_STATUS_OK);
    }
    ipc_service_handle_t overflow = 0;
    assert(ipc_service_register("s-overflow", CAP_FS_CREATE, IPC_RIGHT_MASK,
        IPC_RIGHT_MASK, &overflow) == IPC_STATUS_LIMIT);
    ipc_task_close_all(&task_a);

    active_task = &task_c;
    task_c.capabilities = CAP_FS_CREATE;
    ipc_service_handle_t service = 0;
    active_task = &task_a;
    assert(ipc_service_register("com.charis.capacity", CAP_FS_CREATE,
        IPC_RIGHT_MASK, IPC_RIGHT_MASK, &service) == IPC_STATUS_OK);
    active_task = &task_c;
    ipc_request_handle_t requests[IPC_MAX_CONNECT_REQUESTS];
    for (u32 i = 0; i < IPC_MAX_CONNECT_REQUESTS; i++)
        assert(ipc_service_connect("com.charis.capacity", &requests[i]) ==
               IPC_STATUS_OK);
    ipc_request_handle_t request_overflow = 0;
    assert(ipc_service_connect("com.charis.capacity", &request_overflow) ==
           IPC_STATUS_LIMIT);
    ipc_task_close_all(&task_c);
    active_task = &task_a;
    ipc_request_handle_t next = 0;
    u32 client_pid = 0;
    assert(ipc_service_next(service, &next, &client_pid) == IPC_STATUS_EMPTY);
    assert(ipc_service_unregister(service) == IPC_STATUS_OK);
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
int main(void) {
    reset_task(&task_a, 101); reset_task(&task_b, 202); reset_task(&task_c, 303);
    ipc_init();
    test_pair_validation();
    test_directional_rights();
    test_waiter_message_wakeup();
    test_waiter_spurious_wakeup();
    test_waiter_peer_close_wakeup();
    test_waiter_cancelled_on_task_teardown();
    test_rights_queue_and_close();
    test_stale_handle_and_teardown();
    test_service_registration_and_policy();
    test_service_rejection_and_unregister_pending();
    test_service_request_lifecycle_on_task_exit();
    test_service_and_request_table_limits();
    assert(ipc_test_service_generation_exhaustion());
    assert(ipc_test_request_generation_exhaustion());
    test_generation_exhaustion();
    test_table_limit();
    puts("PASS: task-owned IPC handles, rights, bounded queues and teardown");
    return 0;
}
